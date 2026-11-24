#pragma once

#include "camera.hpp"
#include "gl_loader.hpp"
#include "hud.hpp"
#include "renderer.hpp"
#include "stl_loader.hpp"
#include "viewer_state.hpp"

#include <X11/Xlib.h>
#include <string>

namespace faset {

class X11GLWindow {
public:
    ~X11GLWindow() { close(); }

    bool open(const std::string& title, int width, int height, bool vsync, std::string& err);
    void close();

    void set_title(const std::string& title);
    void swap_buffers();

    Display* display() const { return dpy_; }
    Window window() const { return win_; }
    int connection_fd() const;
    int width() const { return width_; }
    int height() const { return height_; }
    void update_size(int w, int h) { width_ = w; height_ = h; }
    GlFuncs& gl() { return gl_; }

    // Atoms exposed for event loop
    Atom wm_delete_window() const { return wm_delete_; }
    Atom xdnd_enter() const { return xdnd_enter_; }
    Atom xdnd_position() const { return xdnd_position_; }
    Atom xdnd_status() const { return xdnd_status_; }
    Atom xdnd_drop() const { return xdnd_drop_; }
    Atom xdnd_finished() const { return xdnd_finished_; }
    Atom xdnd_selection() const { return xdnd_selection_; }
    Atom xdnd_type_list() const { return xdnd_type_list_; }
    Atom xdnd_action_copy() const { return xdnd_action_copy_; }
    Atom drop_prop() const { return drop_prop_; }
    Atom text_uri_list() const { return text_uri_list_; }

private:
    Display* dpy_ = nullptr;
    Window win_ = 0;
    Colormap cmap_ = 0;
    GLXContext ctx_ = nullptr;
    GlFuncs gl_{};
    int width_ = 1280;
    int height_ = 720;

    Atom wm_delete_ = None;
    Atom net_wm_name_ = None;
    Atom utf8_string_ = None;
    Atom net_wm_icon_ = None;

    Atom xdnd_aware_ = None;
    Atom xdnd_enter_ = None;
    Atom xdnd_position_ = None;
    Atom xdnd_status_ = None;
    Atom xdnd_drop_ = None;
    Atom xdnd_finished_ = None;
    Atom xdnd_selection_ = None;
    Atom xdnd_type_list_ = None;
    Atom xdnd_action_copy_ = None;
    Atom drop_prop_ = None;
    Atom text_uri_list_ = None;

    void set_window_icon();
};

int run_viewer(const std::string& initial_stl_path);
int run_self_test_gl(const std::string& stl_path);

struct ScreenshotOpts {
    std::string out_ppm;
    std::string stl_path;
    int width = 1280;
    int height = 720;
    bool dark_theme = true;
    bool slice = false;
    bool wire = false;
    bool xray = false;
    bool two_sided = false;
    bool show_hud = true;
    int matcap = 0;
};
int run_screenshot(const ScreenshotOpts& opts);

}  // namespace faset
