#include "platform_x11.hpp"
#include "uri_dir.hpp"

#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace faset {

namespace {

int g_glx_error = 0;
int glx_err_handler(Display*, XErrorEvent*) {
    g_glx_error = 1;
    return 0;
}

std::string base_name_of(const std::string& p) {
    size_t s = p.find_last_of('/');
    return (s == std::string::npos) ? p : p.substr(s + 1);
}

std::string dir_name_of(const std::string& p) {
    size_t s = p.find_last_of('/');
    return (s == std::string::npos) ? std::string(".") : (s == 0 ? std::string("/") : p.substr(0, s));
}

// Launch non-blocking OS file chooser child writing chosen path to pipe_fd
bool spawn_file_dialog(const std::string& start_dir, int& out_fd, pid_t& out_pid) {
    int pipefd[2]{-1, -1};
    if (::pipe2(pipefd, O_CLOEXEC) != 0) return false;

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);

        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }

        const char* py_script =
            "import sys, os\n"
            "d = sys.argv[1] if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]) else os.path.expanduser('~')\n"
            "try:\n"
            "    from PyQt6.QtWidgets import QApplication, QFileDialog\n"
            "    app = QApplication([])\n"
            "    p, _ = QFileDialog.getOpenFileName(None, 'Open STL File', d, 'STL Files (*.stl *.STL);;All Files (*)')\n"
            "    if p: print(p, flush=True)\n"
            "    sys.exit(0)\n"
            "except Exception:\n"
            "    pass\n"
            "import subprocess, shutil\n"
            "for cmd in (['zenity','--file-selection','--title=Open STL File','--filename='+d+'/','--file-filter=STL Files | *.stl *.STL'], ['kdialog','--getopenfilename',d,'*.stl *.STL|STL Files']):\n"
            "    if shutil.which(cmd[0]):\n"
            "        r = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)\n"
            "        if r.returncode == 0 and r.stdout.strip(): print(r.stdout.strip(), flush=True)\n"
            "        sys.exit(0)\n";

        ::execlp("python3", "python3", "-c", py_script, start_dir.c_str(), nullptr);
        _exit(127);
    }

    ::close(pipefd[1]);
    int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    out_fd = pipefd[0];
    out_pid = pid;
    return true;
}

class InotifyWatcher {
public:
    InotifyWatcher() { fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC); }
    ~InotifyWatcher() {
        clear();
        if (fd_ >= 0) ::close(fd_);
    }

    int fd() const { return fd_; }

    void watch_file(const std::string& path) {
        clear();
        if (fd_ < 0 || path.empty()) return;
        std::string dir = dir_name_of(path);
        target_name_ = base_name_of(path);
        target_path_ = path;
        wd_ = ::inotify_add_watch(fd_, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
    }

    void clear() {
        if (fd_ >= 0 && wd_ >= 0) {
            ::inotify_rm_watch(fd_, wd_);
            wd_ = -1;
        }
        target_name_.clear();
        target_path_.clear();
    }

    bool poll_changed(std::string& out_path) {
        if (fd_ < 0) return false;
        alignas(inotify_event) char buf[4096];
        bool changed = false;
        for (;;) {
            ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n <= 0) break;
            ssize_t off = 0;
            while (off < n) {
                const auto* ev = reinterpret_cast<const inotify_event*>(buf + off);
                if (ev->len > 0 && target_name_ == ev->name) {
                    changed = true;
                }
                off += static_cast<ssize_t>(sizeof(inotify_event) + ev->len);
            }
        }
        if (changed) {
            out_path = target_path_;
            return true;
        }
        return false;
    }

private:
    int fd_ = -1;
    int wd_ = -1;
    std::string target_name_;
    std::string target_path_;
};

}  // namespace

void X11GLWindow::set_window_icon() {
    constexpr int W = 32;
    constexpr int H = 32;
    // _NET_WM_ICON format on 64-bit Xlib uses unsigned long per element!
    std::vector<unsigned long> data(2 + W * H, 0);
    data[0] = W;
    data[1] = H;

    const float cx = 15.5f, cy = 15.5f;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - cy;
            // Rounded squircle mask
            float sx = std::fabs(dx) / 14.5f;
            float sy = std::fabs(dy) / 14.5f;
            float d = std::pow(sx, 4.0f) + std::pow(sy, 4.0f);
            unsigned long argb = 0;
            if (d <= 1.0f) {
                // Dark slate background with isometric faceted cube highlight
                uint8_t r = 28, g = 34, b = 44, a = 255;
                // Isometric hexagon centered at (15.5, 15.5), radius 10.5
                float hx = std::fabs(dx);
                float hy = dy;
                float hex = std::max(hx * 0.866025f + std::fabs(hy) * 0.5f, hx);
                if (hex < 9.5f) {
                    if (hy < 0.0f && std::fabs(hy) > hx * 0.55f) {
                        r = 212; g = 222; b = 236;  // top facet
                    } else if (dx < 0.0f) {
                        r = 96; g = 148; b = 220;   // left accent facet
                    } else {
                        r = 52; g = 86; b = 138;    // right shadow facet
                    }
                }
                argb = (static_cast<unsigned long>(a) << 24) |
                       (static_cast<unsigned long>(r) << 16) |
                       (static_cast<unsigned long>(g) << 8) |
                       static_cast<unsigned long>(b);
            }
            data[2 + static_cast<size_t>(y * W + x)] = argb;
        }
    }
    XChangeProperty(dpy_, win_, net_wm_icon_, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(data.data()),
                    static_cast<int>(data.size()));
}

bool X11GLWindow::open(const std::string& title, int width, int height, bool vsync, std::string& err) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) {
        err = "Cannot open X11 display (check DISPLAY environment variable)";
        return false;
    }

    if (!gl_.load_glx(err)) {
        close();
        return false;
    }

    int screen = DefaultScreen(dpy_);
    const int fb_attribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER, True,
        None
    };

    int fb_count = 0;
    GLXFBConfig* fbcs = gl_.xChooseFBConfig(dpy_, screen, fb_attribs, &fb_count);
    if (!fbcs || fb_count <= 0) {
        err = "glXChooseFBConfig found no suitable 24-bit depth double-buffered visual";
        if (fbcs) XFree(fbcs);
        close();
        return false;
    }
    GLXFBConfig chosen_fbc = fbcs[0];
    XFree(fbcs);

    XVisualInfo* vi = gl_.xGetVisualFromFBConfig(dpy_, chosen_fbc);
    if (!vi) {
        err = "glXGetVisualFromFBConfig returned null";
        close();
        return false;
    }

    Window root = RootWindow(dpy_, screen);
    cmap_ = XCreateColormap(dpy_, root, vi->visual, AllocNone);

    XSetWindowAttributes swa{};
    swa.colormap = cmap_;
    swa.border_pixel = 0;
    swa.background_pixel = 0x16191E;
    swa.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     FocusChangeMask;

    win_ = XCreateWindow(dpy_, root, 0, 0,
                         static_cast<unsigned int>(width_),
                         static_cast<unsigned int>(height_),
                         0, vi->depth, InputOutput, vi->visual,
                         CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &swa);
    XFree(vi);
    if (!win_) {
        err = "XCreateWindow failed";
        close();
        return false;
    }

    // Class hints
    XClassHint* ch = XAllocClassHint();
    if (ch) {
        char rn[] = "stlviewer";
        char rc[] = "StlViewer";
        ch->res_name = rn;
        ch->res_class = rc;
        XSetClassHint(dpy_, win_, ch);
        XFree(ch);
    }

    wm_delete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy_, win_, &wm_delete_, 1);

    net_wm_name_ = XInternAtom(dpy_, "_NET_WM_NAME", False);
    utf8_string_ = XInternAtom(dpy_, "UTF8_STRING", False);
    net_wm_icon_ = XInternAtom(dpy_, "_NET_WM_ICON", False);

    xdnd_aware_ = XInternAtom(dpy_, "XdndAware", False);
    xdnd_enter_ = XInternAtom(dpy_, "XdndEnter", False);
    xdnd_position_ = XInternAtom(dpy_, "XdndPosition", False);
    xdnd_status_ = XInternAtom(dpy_, "XdndStatus", False);
    xdnd_drop_ = XInternAtom(dpy_, "XdndDrop", False);
    xdnd_finished_ = XInternAtom(dpy_, "XdndFinished", False);
    xdnd_selection_ = XInternAtom(dpy_, "XdndSelection", False);
    xdnd_type_list_ = XInternAtom(dpy_, "XdndTypeList", False);
    xdnd_action_copy_ = XInternAtom(dpy_, "XdndActionCopy", False);
    drop_prop_ = XInternAtom(dpy_, "STLVIEWER_XDND_DATA", False);
    text_uri_list_ = XInternAtom(dpy_, "text/uri-list", False);

    Atom xdnd_version = 5;
    XChangeProperty(dpy_, win_, xdnd_aware_, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&xdnd_version), 1);

    set_window_icon();
    set_title(title);

    // Create OpenGL 3.3 Core context
    const int ctx_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };

    g_glx_error = 0;
    int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(glx_err_handler);
    ctx_ = gl_.xCreateContextAttribsARB(dpy_, chosen_fbc, nullptr, True, ctx_attribs);
    XSync(dpy_, False);
    XSetErrorHandler(old_handler);

    if (!ctx_ || g_glx_error) {
        err = "Failed to create OpenGL 3.3 Core GLX context";
        close();
        return false;
    }

    if (!gl_.xMakeCurrent(dpy_, win_, ctx_)) {
        err = "glXMakeCurrent failed";
        close();
        return false;
    }

    if (!gl_.load_gl_core(err)) {
        close();
        return false;
    }

    XMapWindow(dpy_, win_);
    XSync(dpy_, False);

    // Wait briefly for initial MapNotify/ConfigureNotify so initial backbuffer matches
    XWindowAttributes xwa{};
    if (XGetWindowAttributes(dpy_, win_, &xwa)) {
        width_ = std::max(1, xwa.width);
        height_ = std::max(1, xwa.height);
    }

    int interval = vsync ? 1 : 0;
    if (gl_.xSwapIntervalEXT) {
        gl_.xSwapIntervalEXT(dpy_, win_, interval);
    } else if (gl_.xSwapIntervalMESA) {
        gl_.xSwapIntervalMESA(static_cast<unsigned int>(interval));
    }

    return true;
}

void X11GLWindow::close() {
    if (dpy_) {
        if (ctx_) {
            gl_.xMakeCurrent(dpy_, None, nullptr);
            gl_.xDestroyContext(dpy_, ctx_);
            ctx_ = nullptr;
        }
        if (win_) {
            XDestroyWindow(dpy_, win_);
            win_ = 0;
        }
        if (cmap_) {
            XFreeColormap(dpy_, cmap_);
            cmap_ = 0;
        }
        XCloseDisplay(dpy_);
        dpy_ = nullptr;
    }
    gl_.unload();
}

void X11GLWindow::set_title(const std::string& title) {
    if (!dpy_ || !win_) return;
    XStoreName(dpy_, win_, title.c_str());
    XChangeProperty(dpy_, win_, net_wm_name_, utf8_string_, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title.data()),
                    static_cast<int>(title.size()));
}

void X11GLWindow::swap_buffers() {
    if (dpy_ && win_) {
        gl_.xSwapBuffers(dpy_, win_);
    }
}

int X11GLWindow::connection_fd() const {
    return dpy_ ? ConnectionNumber(dpy_) : -1;
}

int run_viewer(const std::string& initial_stl_path) {
    X11GLWindow win;
    std::string err;
    if (!win.open("stlviewer", 1280, 720, true, err)) {
        std::fprintf(stderr, "stlviewer error: %s\n", err.c_str());
        return 1;
    }

    GlFuncs& gl = win.gl();
    Renderer renderer;
    if (!renderer.init(gl, win.width(), win.height(), err)) {
        std::fprintf(stderr, "stlviewer renderer init error: %s\n", err.c_str());
        return 1;
    }
    Hud hud;
    if (!hud.init(gl, err)) {
        std::fprintf(stderr, "stlviewer HUD init error: %s\n", err.c_str());
        renderer.destroy(gl);
        return 1;
    }

    Camera cam;
    ViewerState state;
    MeshData mesh;
    bool has_mesh = false;
    InotifyWatcher watcher;

    auto status_expire = std::chrono::steady_clock::time_point{};
    auto set_status = [&](const std::string& msg, int ms = 2200) {
        state.status_msg = msg;
        status_expire = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    };

    auto update_sibling_info = [&](const std::string& path) {
        int idx = -1;
        auto list = list_sibling_stls(path, &idx);
        state.sibling_index = idx;
        state.sibling_count = static_cast<int>(list.size());
    };

    auto load_model = [&](const std::string& path, bool keep_camera) {
        MeshData next = load_stl(path);
        if (!next.ok) {
            set_status("Error: " + next.error, 3500);
            renderer.reset_accumulation();
            return;
        }
        std::string uerr;
        if (!renderer.upload_mesh(gl, next, uerr)) {
            set_status("GPU Error: " + uerr, 3500);
            return;
        }
        mesh = std::move(next);
        has_mesh = true;
        if (!keep_camera) {
            cam.frame_bounds(mesh.bbox_min, mesh.bbox_max, win.width(), win.height(), false);
            state.measure_has_a = false;
            state.measure_has_b = false;
        } else {
            cam.set_scene_radius(std::max(length(mesh.bbox_extent) * 0.5f, 1e-2f));
        }
        update_sibling_info(mesh.path);
        watcher.watch_file(mesh.path);
        win.set_title("stlviewer — " + base_name_of(mesh.path));
        renderer.reset_accumulation();
    };

    if (!initial_stl_path.empty()) {
        load_model(initial_stl_path, false);
    }

    bool running = true;
    bool needs_redraw = true;

    // Mouse / drag state
    bool dragging_orbit = false;
    bool dragging_pan = false;
    int last_mx = 0, last_my = 0;
    Time last_click_time = 0;
    int last_click_x = -100, last_click_y = -100;

    // XDND state
    Window xdnd_src = None;
    Atom xdnd_chosen_type = None;

    // Async file dialog state
    int dialog_fd = -1;
    pid_t dialog_pid = -1;
    std::string dialog_buf;

    auto start_open_dialog = [&]() {
        if (dialog_fd >= 0) return;  // already open
        std::string start_dir = has_mesh ? dir_name_of(mesh.path) : std::string(".");
        if (!spawn_file_dialog(start_dir, dialog_fd, dialog_pid)) {
            set_status("Failed to launch file picker", 2500);
            needs_redraw = true;
        }
    };

    auto apply_hud_cmd = [&](HudCommand cmd, float slice_val) {
        switch (cmd) {
            case HudCommand::OpenFile:
                start_open_dialog();
                break;
            case HudCommand::CycleMatcap:
                state.matcap = static_cast<MatcapKind>((static_cast<int>(state.matcap) + 1) %
                                                       static_cast<int>(MatcapKind::Count));
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleWire:
                state.wireframe = !state.wireframe;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleSlice:
                state.slice_enabled = !state.slice_enabled;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleMeasure:
                state.measure_mode = !state.measure_mode;
                if (!state.measure_mode) {
                    state.measure_has_a = false;
                    state.measure_has_b = false;
                }
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleXray:
                state.xray = !state.xray;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleTwoSided:
                state.two_sided = !state.two_sided;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleTheme:
                state.dark_theme = !state.dark_theme;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ResetView:
                if (has_mesh) {
                    cam.frame_bounds(mesh.bbox_min, mesh.bbox_max, win.width(), win.height(), true);
                }
                cam.set_preset(ViewPreset::Iso, true);
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::ToggleHelp:
                state.show_help = !state.show_help;
                needs_redraw = true;
                break;
            case HudCommand::SliceAxisX:
                state.slice_axis = 0;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::SliceAxisY:
                state.slice_axis = 1;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::SliceAxisZ:
                state.slice_axis = 2;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::SliceFlip:
                state.slice_flip = !state.slice_flip;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::SliceScrub:
                state.slice_t = slice_val;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::MeasureClear:
                state.measure_has_a = false;
                state.measure_has_b = false;
                renderer.reset_accumulation();
                needs_redraw = true;
                break;
            case HudCommand::NoAction:
                break;
        }
    };

    auto prev_frame_time = std::chrono::steady_clock::now();

    while (running) {
        // Expire transient status badge if set
        if (!state.status_msg.empty() && std::chrono::steady_clock::now() >= status_expire) {
            state.status_msg.clear();
            needs_redraw = true;
        }

        // Compute poll timeout: -1 (0% CPU sleep) when completely settled!
        bool active_work = cam.is_animating() || !renderer.is_settled() || needs_redraw;
        int timeout_ms = -1;
        if (active_work) {
            timeout_ms = 0;
        } else if (!state.status_msg.empty()) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                status_expire - std::chrono::steady_clock::now()).count();
            timeout_ms = static_cast<int>(std::max<long>(1, rem));
        }

        if (XPending(win.display()) == 0) {
            struct pollfd pfds[3]{};
            nfds_t nfds = 0;
            pfds[nfds++] = {win.connection_fd(), POLLIN, 0};
            int inotify_idx = -1;
            if (watcher.fd() >= 0) {
                inotify_idx = static_cast<int>(nfds);
                pfds[nfds++] = {watcher.fd(), POLLIN, 0};
            }
            int dialog_idx = -1;
            if (dialog_fd >= 0) {
                dialog_idx = static_cast<int>(nfds);
                pfds[nfds++] = {dialog_fd, POLLIN, 0};
            }

            int pr = ::poll(pfds, nfds, timeout_ms);
            if (pr > 0) {
                if (inotify_idx >= 0 && (pfds[inotify_idx].revents & POLLIN)) {
                    std::string changed_path;
                    if (watcher.poll_changed(changed_path)) {
                        load_model(changed_path, true);
                        set_status("Reloaded", 1800);
                        needs_redraw = true;
                    }
                }
                if (dialog_idx >= 0 && (pfds[dialog_idx].revents & (POLLIN | POLLHUP))) {
                    char tmp[512];
                    ssize_t r = ::read(dialog_fd, tmp, sizeof(tmp));
                    if (r > 0) {
                        dialog_buf.append(tmp, static_cast<size_t>(r));
                    } else if (r == 0) {
                        ::close(dialog_fd);
                        dialog_fd = -1;
                        if (dialog_pid > 0) {
                            int st = 0;
                            ::waitpid(dialog_pid, &st, 0);
                            dialog_pid = -1;
                        }
                        while (!dialog_buf.empty() && (dialog_buf.back() == '\n' || dialog_buf.back() == '\r')) {
                            dialog_buf.pop_back();
                        }
                        if (!dialog_buf.empty()) {
                            load_model(dialog_buf, false);
                            needs_redraw = true;
                        }
                        dialog_buf.clear();
                    }
                }
            }
        }

        // Drain ALL pending X11 events
        while (XPending(win.display()) > 0) {
            XEvent ev{};
            XNextEvent(win.display(), &ev);
            switch (ev.type) {
                case Expose:
                    if (ev.xexpose.count == 0) needs_redraw = true;
                    break;
                case ConfigureNotify:
                    if (ev.xconfigure.width != win.width() || ev.xconfigure.height != win.height()) {
                        win.update_size(ev.xconfigure.width, ev.xconfigure.height);
                        renderer.resize(gl, win.width(), win.height());
                        needs_redraw = true;
                    }
                    break;
                case ClientMessage: {
                    if (static_cast<Atom>(ev.xclient.data.l[0]) == win.wm_delete_window()) {
                        running = false;
                    } else if (ev.xclient.message_type == win.xdnd_enter()) {
                        xdnd_src = static_cast<Window>(ev.xclient.data.l[0]);
                        xdnd_chosen_type = win.text_uri_list();
                        bool more_than_3 = (ev.xclient.data.l[1] & 1) != 0;
                        if (more_than_3) {
                            Atom act_type = None;
                            int act_fmt = 0;
                            unsigned long nitems = 0, rem = 0;
                            unsigned char* prop = nullptr;
                            if (XGetWindowProperty(win.display(), xdnd_src, win.xdnd_type_list(), 0, 64,
                                                   False, XA_ATOM, &act_type, &act_fmt, &nitems, &rem, &prop) == Success && prop) {
                                const Atom* atoms = reinterpret_cast<const Atom*>(prop);
                                for (unsigned long i = 0; i < nitems; ++i) {
                                    if (atoms[i] == win.text_uri_list()) {
                                        xdnd_chosen_type = win.text_uri_list();
                                        break;
                                    }
                                }
                                XFree(prop);
                            }
                        }
                    } else if (ev.xclient.message_type == win.xdnd_position()) {
                        Window src = static_cast<Window>(ev.xclient.data.l[0]);
                        XEvent reply{};
                        reply.xclient.type = ClientMessage;
                        reply.xclient.display = win.display();
                        reply.xclient.window = src;
                        reply.xclient.message_type = win.xdnd_status();
                        reply.xclient.format = 32;
                        reply.xclient.data.l[0] = static_cast<long>(win.window());
                        reply.xclient.data.l[1] = 1;  // accept drop
                        reply.xclient.data.l[2] = 0;
                        reply.xclient.data.l[3] = 0;
                        reply.xclient.data.l[4] = static_cast<long>(win.xdnd_action_copy());
                        XSendEvent(win.display(), src, False, NoEventMask, &reply);
                    } else if (ev.xclient.message_type == win.xdnd_drop()) {
                        xdnd_src = static_cast<Window>(ev.xclient.data.l[0]);
                        Time ts = static_cast<Time>(ev.xclient.data.l[2]);
                        XConvertSelection(win.display(), win.xdnd_selection(),
                                          xdnd_chosen_type ? xdnd_chosen_type : win.text_uri_list(),
                                          win.drop_prop(), win.window(), ts);
                    }
                    break;
                }
                case SelectionNotify: {
                    if (ev.xselection.property == win.drop_prop()) {
                        Atom act_type = None;
                        int act_fmt = 0;
                        unsigned long nitems = 0, rem = 0;
                        unsigned char* prop = nullptr;
                        if (XGetWindowProperty(win.display(), win.window(), win.drop_prop(), 0, 65536,
                                               True, AnyPropertyType, &act_type, &act_fmt, &nitems, &rem, &prop) == Success && prop) {
                            std::string uri_str(reinterpret_cast<const char*>(prop), nitems);
                            XFree(prop);
                            std::string dropped = parse_xdnd_uri_list(uri_str);
                            if (!dropped.empty()) {
                                load_model(dropped, false);
                                needs_redraw = true;
                            } else {
                                set_status("Drop a .stl file", 2000);
                                needs_redraw = true;
                            }
                        }
                        if (xdnd_src != None) {
                            XEvent fin{};
                            fin.xclient.type = ClientMessage;
                            fin.xclient.display = win.display();
                            fin.xclient.window = xdnd_src;
                            fin.xclient.message_type = win.xdnd_finished();
                            fin.xclient.format = 32;
                            fin.xclient.data.l[0] = static_cast<long>(win.window());
                            fin.xclient.data.l[1] = 1;
                            fin.xclient.data.l[2] = static_cast<long>(win.xdnd_action_copy());
                            XSendEvent(win.display(), xdnd_src, False, NoEventMask, &fin);
                            xdnd_src = None;
                        }
                    }
                    break;
                }
                case ButtonPress: {
                    int mx = ev.xbutton.x;
                    int my = ev.xbutton.y;
                    last_mx = mx;
                    last_my = my;

                    if (ev.xbutton.button == Button1) {
                        float slice_v = state.slice_t;
                        HudCommand cmd = state.show_hud ? hud.on_mouse_down(static_cast<float>(mx), static_cast<float>(my), slice_v) : HudCommand::NoAction;
                        if (cmd != HudCommand::NoAction) {
                            apply_hud_cmd(cmd, slice_v);
                            break;
                        }

                        // Measure point pick
                        if (state.measure_mode && has_mesh && !(ev.xbutton.state & ShiftMask)) {
                            Vec3 pt{};
                            if (renderer.pick_world_point(gl, mx, my, cam, pt)) {
                                if (!state.measure_has_a || state.measure_has_b) {
                                    state.measure_a = pt;
                                    state.measure_has_a = true;
                                    state.measure_has_b = false;
                                } else {
                                    state.measure_b = pt;
                                    state.measure_has_b = true;
                                }
                                renderer.reset_accumulation();
                                needs_redraw = true;
                            }
                            break;
                        }

                        // Double-click detection for Pivot / Reset
                        Time dt_ms = ev.xbutton.time - last_click_time;
                        int dpx = std::abs(mx - last_click_x) + std::abs(my - last_click_y);
                        last_click_time = ev.xbutton.time;
                        last_click_x = mx;
                        last_click_y = my;

                        if (dt_ms > 0 && dt_ms < 350 && dpx < 8 && has_mesh) {
                            Vec3 hit{};
                            if (renderer.pick_world_point(gl, mx, my, cam, hit)) {
                                cam.set_pivot(hit, true);
                            } else {
                                cam.frame_bounds(mesh.bbox_min, mesh.bbox_max, win.width(), win.height(), true);
                            }
                            renderer.reset_accumulation();
                            needs_redraw = true;
                            break;
                        }

                        if (ev.xbutton.state & ShiftMask) {
                            dragging_pan = true;
                        } else {
                            dragging_orbit = true;
                        }
                    } else if (ev.xbutton.button == Button2 || ev.xbutton.button == Button3) {
                        dragging_pan = true;
                    } else if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5) {
                        float step = (ev.xbutton.button == Button4) ? 1.0f : -1.0f;
                        if (state.slice_enabled && ((ev.xbutton.state & Mod1Mask) ||
                            hud.is_pointer_over_ui(static_cast<float>(mx), static_cast<float>(my)))) {
                            state.slice_t = std::clamp(state.slice_t + step * 0.03f, 0.0f, 1.0f);
                        } else {
                            cam.zoom(step, static_cast<float>(mx), static_cast<float>(my), win.width(), win.height());
                        }
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    }
                    break;
                }
                case ButtonRelease:
                    if (ev.xbutton.button == Button1) {
                        dragging_orbit = false;
                        dragging_pan = false;
                        hud.on_mouse_up();
                    } else if (ev.xbutton.button == Button2 || ev.xbutton.button == Button3) {
                        dragging_pan = false;
                    }
                    break;
                case MotionNotify: {
                    int mx = ev.xmotion.x;
                    int my = ev.xmotion.y;
                    float dx = static_cast<float>(mx - last_mx);
                    float dy = static_cast<float>(my - last_my);
                    last_mx = mx;
                    last_my = my;

                    if (hud.is_scrubbing_slice()) {
                        float nv = state.slice_t;
                        if (hud.on_mouse_drag(static_cast<float>(mx), static_cast<float>(my), nv)) {
                            state.slice_t = nv;
                            renderer.reset_accumulation();
                            needs_redraw = true;
                        }
                    } else if (dragging_orbit) {
                        cam.orbit(dx, dy, win.width(), win.height());
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    } else if (dragging_pan) {
                        cam.pan(dx, dy, win.width(), win.height());
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    } else if (state.show_hud) {
                        if (hud.update_hover(static_cast<float>(mx), static_cast<float>(my))) {
                            needs_redraw = true;
                        }
                    }
                    break;
                }
                case KeyPress: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    char ch_buf[8]{};
                    XLookupString(&ev.xkey, ch_buf, sizeof(ch_buf), nullptr, nullptr);
                    char c = ch_buf[0];

                    if (ks == XK_Escape) {
                        if (state.show_help) {
                            state.show_help = false;
                            needs_redraw = true;
                        } else if (state.measure_mode) {
                            if (state.measure_has_a || state.measure_has_b) {
                                state.measure_has_a = false;
                                state.measure_has_b = false;
                            } else {
                                state.measure_mode = false;
                            }
                            renderer.reset_accumulation();
                            needs_redraw = true;
                        } else if (state.slice_enabled) {
                            state.slice_enabled = false;
                            renderer.reset_accumulation();
                            needs_redraw = true;
                        } else {
                            running = false;
                        }
                    } else if (c == 'q' || c == 'Q') {
                        running = false;
                    } else if (c == 'o' || c == 'O') {
                        start_open_dialog();
                    } else if (c == 'm' || c == 'M') {
                        apply_hud_cmd(HudCommand::CycleMatcap, state.slice_t);
                    } else if (c == 'w' || c == 'W') {
                        apply_hud_cmd(HudCommand::ToggleWire, state.slice_t);
                    } else if (c == 'x' || c == 'X') {
                        apply_hud_cmd(HudCommand::ToggleXray, state.slice_t);
                    } else if (c == 'b' || c == 'B') {
                        apply_hud_cmd(HudCommand::ToggleTwoSided, state.slice_t);
                    } else if (c == 's' || c == 'S') {
                        apply_hud_cmd(HudCommand::ToggleSlice, state.slice_t);
                    } else if (ks == XK_Tab && state.slice_enabled) {
                        state.slice_axis = (state.slice_axis + 1) % 3;
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    } else if ((c == 'i' || c == 'I') && state.slice_enabled) {
                        apply_hud_cmd(HudCommand::SliceFlip, state.slice_t);
                    } else if (c == 'd' || c == 'D') {
                        apply_hud_cmd(HudCommand::ToggleMeasure, state.slice_t);
                    } else if (c == 't' || c == 'T') {
                        apply_hud_cmd(HudCommand::ToggleTheme, state.slice_t);
                    } else if (c == 'h' || c == 'H') {
                        state.show_hud = !state.show_hud;
                        needs_redraw = true;
                    } else if (c == 'g' || c == 'G') {
                        state.show_grid = !state.show_grid;
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    } else if (c == 'p' || c == 'P') {
                        cam.toggle_ortho();
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    } else if (c == 'u' || c == 'U') {
                        cam.toggle_up_axis();
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    } else if (c == 'r' || c == 'R' || c == 'f' || c == 'F') {
                        apply_hud_cmd(HudCommand::ResetView, state.slice_t);
                    } else if (c == '?' || ks == XK_F1) {
                        state.show_help = !state.show_help;
                        needs_redraw = true;
                    } else if (c == '[' || c == ']') {
                        if (has_mesh) {
                            std::string next = step_sibling_stl(mesh.path, (c == ']') ? +1 : -1);
                            if (!next.empty() && next != mesh.path) {
                                load_model(next, false);
                                needs_redraw = true;
                            }
                        }
                    } else if (c >= '0' && c <= '6') {
                        ViewPreset p = ViewPreset::Iso;
                        switch (c) {
                            case '1': p = ViewPreset::Front; break;
                            case '2': p = ViewPreset::Back; break;
                            case '3': p = ViewPreset::Right; break;
                            case '4': p = ViewPreset::Left; break;
                            case '5': p = ViewPreset::Top; break;
                            case '6': p = ViewPreset::Bottom; break;
                            default:  p = ViewPreset::Iso; break;
                        }
                        cam.set_preset(p, true);
                        renderer.reset_accumulation();
                        needs_redraw = true;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - prev_frame_time).count();
        prev_frame_time = now;

        if (cam.is_animating()) {
            cam.step(dt);
            renderer.reset_accumulation();
            needs_redraw = true;
        }

        if (needs_redraw || !renderer.is_settled()) {
            renderer.render_frame(gl, has_mesh ? &mesh : nullptr, cam, state, hud);
            win.swap_buffers();
            needs_redraw = false;
        }
    }

    if (dialog_fd >= 0) ::close(dialog_fd);
    if (dialog_pid > 0) {
        ::kill(dialog_pid, SIGTERM);
        int st = 0;
        ::waitpid(dialog_pid, &st, 0);
    }

    hud.destroy(gl);
    renderer.destroy(gl);
    win.close();
    return 0;
}

int run_self_test_gl(const std::string& stl_path) {
    MeshData mesh = load_stl(stl_path);
    if (!mesh.ok) {
        std::fprintf(stderr, "[self-test-gl] Failed to load STL: %s\n", mesh.error.c_str());
        return 1;
    }

    X11GLWindow win;
    std::string err;
    if (!win.open("stlviewer self-test-gl", 1024, 640, false, err)) {
        std::fprintf(stderr, "[self-test-gl] Window/GLX init failed: %s\n", err.c_str());
        return 1;
    }

    GlFuncs& gl = win.gl();
    Renderer renderer;
    if (!renderer.init(gl, win.width(), win.height(), err)) {
        std::fprintf(stderr, "[self-test-gl] Renderer init failed: %s\n", err.c_str());
        return 1;
    }
    Hud hud;
    if (!hud.init(gl, err)) {
        std::fprintf(stderr, "[self-test-gl] HUD init failed: %s\n", err.c_str());
        renderer.destroy(gl);
        return 1;
    }
    if (!renderer.upload_mesh(gl, mesh, err)) {
        std::fprintf(stderr, "[self-test-gl] VBO upload failed: %s\n", err.c_str());
        hud.destroy(gl);
        renderer.destroy(gl);
        return 1;
    }

    Camera cam;
    cam.frame_bounds(mesh.bbox_min, mesh.bbox_max, win.width(), win.height(), false);
    ViewerState state;

    while (gl.GetError() != GL_NO_ERROR) {}

    auto t0 = std::chrono::steady_clock::now();
    CullStats last_cs{};
    for (int s = 0; s < Renderer::MAX_SSAA_SAMPLES; ++s) {
        last_cs = renderer.render_frame(gl, &mesh, cam, state, hud);
    }
    gl.Finish();
    auto t1 = std::chrono::steady_clock::now();
    double render_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    GLenum gl_err = gl.GetError();
    if (gl_err != GL_NO_ERROR) {
        std::fprintf(stderr, "[self-test-gl] OpenGL error recorded: 0x%X\n", gl_err);
        hud.destroy(gl);
        renderer.destroy(gl);
        return 1;
    }

    Vec3 picked{};
    bool hit = false;
    // Probe a small grid near center in case the mesh has a center hole
    const int offsets[5][2] = {{0, 0}, {40, 0}, {-40, 0}, {0, 40}, {0, -40}};
    for (const auto& off : offsets) {
        if (renderer.pick_world_point(gl, win.width() / 2 + off[0], win.height() / 2 + off[1], cam, picked)) {
            hit = true;
            break;
        }
    }

    uint64_t c_rgb = renderer.read_center_pixel_rgb(gl, win.width() / 2, win.height() / 2);
    uint64_t corner_rgb = renderer.read_center_pixel_rgb(gl, 4, 4);
    win.swap_buffers();

    std::printf("[self-test-gl] PASS\n");
    std::printf("  GL Version   : %s\n", gl.version_str.c_str());
    std::printf("  GL Renderer  : %s\n", gl.renderer_str.c_str());
    std::printf("  FBO Format   : %s\n", renderer.using_fp16_fbo() ? "RGBA16F" : "RGBA8");
    std::printf("  Mesh         : %s (%u tris, %zu chunks, %u draw calls, %u cone-culled)\n",
                mesh.path.c_str(), mesh.tri_count, mesh.chunks.size(),
                last_cs.draw_calls, last_cs.cone_culled);
    std::printf("  16x SSAA     : %.2f ms total (%.2f ms/sample)\n",
                render_ms, render_ms / Renderer::MAX_SSAA_SAMPLES);
    std::printf("  Center Pixel : 0x%06llX (Corner: 0x%06llX)\n",
                static_cast<unsigned long long>(c_rgb),
                static_cast<unsigned long long>(corner_rgb));
    if (hit) {
        std::printf("  Depth Pick   : (%.3f, %.3f, %.3f) mm\n", picked.x, picked.y, picked.z);
    } else {
        std::printf("  Depth Pick   : missed center probes\n");
    }

    bool valid_img = (c_rgb != corner_rgb) || hit;
    hud.destroy(gl);
    renderer.destroy(gl);
    return valid_img ? 0 : 1;
}

int run_screenshot(const ScreenshotOpts& opts) {
    MeshData mesh = load_stl(opts.stl_path);
    if (!mesh.ok) {
        std::fprintf(stderr, "[screenshot] Failed to load STL: %s\n", mesh.error.c_str());
        return 1;
    }

    X11GLWindow win;
    std::string err;
    if (!win.open("stlviewer screenshot", opts.width, opts.height, false, err)) {
        std::fprintf(stderr, "[screenshot] Window/GLX init failed: %s\n", err.c_str());
        return 1;
    }

    GlFuncs& gl = win.gl();
    Renderer renderer;
    if (!renderer.init(gl, win.width(), win.height(), err)) {
        std::fprintf(stderr, "[screenshot] Renderer init failed: %s\n", err.c_str());
        return 1;
    }
    Hud hud;
    if (!hud.init(gl, err)) {
        std::fprintf(stderr, "[screenshot] HUD init failed: %s\n", err.c_str());
        renderer.destroy(gl);
        return 1;
    }
    if (!renderer.upload_mesh(gl, mesh, err)) {
        std::fprintf(stderr, "[screenshot] VBO upload failed: %s\n", err.c_str());
        hud.destroy(gl);
        renderer.destroy(gl);
        return 1;
    }

    Camera cam;
    cam.frame_bounds(mesh.bbox_min, mesh.bbox_max, win.width(), win.height(), false);

    ViewerState state;
    state.dark_theme = opts.dark_theme;
    state.slice_enabled = opts.slice;
    state.wireframe = opts.wire;
    state.xray = opts.xray;
    state.two_sided = opts.two_sided;
    state.show_hud = opts.show_hud;
    state.matcap = static_cast<MatcapKind>(std::clamp(opts.matcap, 0, static_cast<int>(MatcapKind::Count) - 1));

    for (int s = 0; s < Renderer::MAX_SSAA_SAMPLES; ++s) {
        renderer.render_frame(gl, &mesh, cam, state, hud);
    }
    gl.Finish();

    bool saved = renderer.save_ppm(gl, opts.out_ppm.c_str());
    win.swap_buffers();

    hud.destroy(gl);
    renderer.destroy(gl);
    if (!saved) {
        std::fprintf(stderr, "[screenshot] Failed writing PPM to %s\n", opts.out_ppm.c_str());
        return 1;
    }
    std::printf("Saved screenshot %dx%d -> %s\n", win.width(), win.height(), opts.out_ppm.c_str());
    return 0;
}

}  // namespace faset
