#include <compositor.h>
#include <vesa.h>
#include <math.h>
#include <kheap.h>
#include <string.h>
#include <bitmap.h>
#include <printf.h>
#include <generic_tree.h>
#include <font.h>
/*
 *
 *
 *  Why not to use black color in this os ??
 *  :( Unfortunately, a hack I used for displaying transparent-background cursor bitmap is that: I ignore all black colors, I view them as transparent color, because 0x00000000 in bmp file means
 *   transparent, this hack is ridiculous, I will fix this!!
 * */
gtree_t * windows_tree;
canvas_t canvas;


/*
 * The window needs to receive user inputs like mouse movement, keyboard keypress, and others.
 * */
void window_message_handler(winmsg_t * msg) {
    window_t * w = msg->window;
    switch(msg->msg_type) {
        case WINMSG_MOUSE:
            if(msg->sub_type == WINMSG_MOUSE_RIGHTCLICK) {
                // Do somethig when mouse right click
                display_all_window();
            }
            if(msg->sub_type == WINMSG_MOUSE_LEFTCLICK) {
                // Translate the cursor_x and cursor_y to base on the window's coord, not the screen's coord
                point_t p = get_canonical_coordinates(w);
                int cursor_x = msg->cursor_x - p.x;
                int cursor_y = msg->cursor_y - p.y;

                if(w->type == WINDOW_NORMAL || w->type == WINDOW_ALERT){
                    // If the point is within the rectangle of the close button, close the window
                    rect_t r = rect_create(w->width - 22, 0, 22, 22);
                    if(w->type == WINDOW_NORMAL || w->type == WINDOW_ALERT) {
                        if(is_point_in_rect(cursor_x, cursor_y, &r)) {
                            close_window(msg->window);
                        }
                    }
                    // Maximize button
                    r.x = w->width - 44;
                    if(is_point_in_rect(cursor_x, cursor_y, &r)) {
                        maximize_window(msg->window);
                    }
                    // Minimize button
                    r.x = w->width - 66;
                    if(is_point_in_rect(cursor_x, cursor_y, &r)) {
                        minimize_window(msg->window);
                    }
                }
                else if(w->type == WINDOW_CONTROL) {
                    // Call button handler
                    if(strcmp("window_xp", w->name) == 0){
                        window_t * alertbox = alertbox_create(w->parent, 100, 100, "Alertbox", "A button is clicked!!!");
                        window_display(alertbox);
                    }
                    else if(strcmp("alertbox_button", w->name) == 0){
                        // Get parent and close
                        close_window(w->parent);
                    }

                }
            }

            break;
        case WINMSG_KEYBOARD:

            break;
        default:
            break;
    }
}

/*
* Convenient function for creating an alertbox window
*/
window_t * alertbox_create(window_t * parent, int x, int y, char * title, char * text) {
    window_t * alertbox = window_create(parent, x, y, 200, 160, WINDOW_ALERT, "window_classic");
    window_add_headline(alertbox, "classic");
    window_add_close_button(alertbox);
    canvas_t canvas_alertbox = canvas_create(alertbox->width, alertbox->height, alertbox->frame_buffer);
    set_font_color(VESA_COLOR_BLACK + 1);
    draw_text(&canvas_alertbox, title, 1, 2);
    draw_text(&canvas_alertbox, text, 5, 2);

    // Add a OK button for the alertbox
    window_t * ok_button = window_create(alertbox, 30, 60, 110, 30, WINDOW_CONTROL, "alertbox_button");
    canvas_t canvas_button = canvas_create(ok_button->width, ok_button->height, ok_button->frame_buffer);
    draw_text(&canvas_button, "Close Button", 1, 1);

    return alertbox;
}


/*
* Window create, given x,y, width,heigt, type, name, and parent window.
*/
window_t *  window_create(window_t * parent, int x, int y, int width, int height, int type, char * name) {
    gtreenode_t * subroot;
    // Allocate spae and initialize
    window_t * w = kcalloc(sizeof(window_t), 1);
    strcpy(w->name, name);
    w->under_windows = list_create();
    w->above_windows = list_create();
    // A window's x and y coordinates should be relative to its parent(when interpreting x/y value of a window)
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    w->type = type;
    w->parent = parent;
    w->is_minimized = 0;
    w->is_maximized = 0;

    w->frame_buffer = kmalloc(width * height * 4);
    if(parent != NULL) {
        if(strcmp(name, "window_red") == 0)
            memsetdw(w->frame_buffer, 0x00ff0000, width * height);
        else if(strcmp(name, "window_green") == 0)
            memsetdw(w->frame_buffer, 0x0000ff00, width * height);
        else if(strcmp(name, "window_blue") == 0)
            memsetdw(w->frame_buffer, 0x000000ff, width * height);
        else if(strcmp(name, "window_black") == 0)
            memsetdw(w->frame_buffer, 0x00000001, width * height);
        else if(strcmp(name, "window_classic") == 0)
            memsetdw(w->frame_buffer, 0x00FBFBF9, width * height);
        else if(strcmp(name, "window_xp") == 0)
            memsetdw(w->frame_buffer, 0x00F7F3F0, width * height);
        else if(strcmp(name, "alertbox_button") == 0)
            memsetdw(w->frame_buffer, 0x00F7F3F0, width * height);
        else if(strcmp(name, "desktop_bar") == 0)
            memsetdw(w->frame_buffer, 0x00DCE8EC , width * height);

    }
    w->fill_color = VESA_COLOR_WHITE;
    w->border_color= VESA_COLOR_BLACK;

    // Add it to the winodw tree
    if(type == WINDOW_SUPER)
        subroot = NULL;
    else if(type == WINDOW_NORMAL)
        subroot = windows_tree->root;
    else {
        // Every window except the super window should have a parent
        subroot = parent->self;
    }

    // Keep a reference to the treenode in window structure
    w->self = tree_insert(windows_tree, subroot, w);

    // Loop through all other windows in the tree, if overlap, add them to list w->under_windows
    add_under_windows(w);
    return w;
}

/*
 * Below is a set of functions that add components to a window by either 1) drawing on the frame buffer of the window, or adding child window(which has its own frame buffer and relative coord)
 * */

/*
 * Draw a headline for the window, given a headline string
 * window headline has height = 22, hardcoded
 * */
void window_add_headline(window_t * w, char * headline) {
    // Draw a rectangle at the start of the window
    set_fill_color(0x00DCE8EC);
    canvas_t canvas = canvas_create(w->width, w->height, w->frame_buffer);
    draw_rect(&canvas, 0, 0, w->width, 22);
    set_fill_color(0x00D3D3D3);
    draw_line(&canvas, 0, 22, w->width, 22);
}

/*
 * Draw a close button for headline (size is 22*22)
 * */
void window_add_close_button(window_t * w) {
    set_fill_color(0x00ff0000);
    canvas_t canvas = canvas_create(w->width, w->height, w->frame_buffer);
    draw_rect(&canvas, w->width - 22, 0, 22, 22);
}

/*
 * Draw a minimize button for headline
 * */
void window_add_minimize_button(window_t * w) {
    set_fill_color(0x0000ff00);
    canvas_t canvas = canvas_create(w->width, w->height, w->frame_buffer);
    draw_rect(&canvas, w->width - 44, 0, 22, 22);
}

/*
 * Draw a close button for headline
 * */
void window_add_maximize_button(window_t * w) {
    set_fill_color(0x00ffff00);
    canvas_t canvas = canvas_create(w->width, w->height, w->frame_buffer);
    draw_rect(&canvas, w->width - 66, 0, 22, 22);
}


/*
* Recursively find out what windows are under window w
*/
void add_under_windows(window_t * w) {
    recur_add_under_windows(w, windows_tree->root);
}

/*
* Helper function for add_under_windows
*/
void recur_add_under_windows(window_t * w, gtreenode_t * subroot) {
    // Stop exploring any branches under w
    window_t * curr_w = subroot->value;
    if(curr_w == w) return;

    if(is_window_overlap(w, curr_w)) {
        list_insert_back(w->under_windows, curr_w);
        list_insert_back(curr_w->above_windows, w);
    }
    foreach(child, subroot->children) {
        recur_add_under_windows(w, child->val);
    }
}

/*
 * Correct way to do it:
 * draw it in the window specified in the parameter
 * Now, i m just going to draw it in the screen frame buffer
 * */
void window_display(window_t * w) {
    if(w->is_minimized) return;
    set_fill_color(w->border_color);


    // If the frame buffer is not null, also draw the frame buffer
    if(w->frame_buffer) {
        rect_region_t rect_reg;
        rect_reg.r.x = w->x;
        rect_reg.r.y = w->y;
        point_t p = get_canonical_coordinates(w);
        rect_reg.r.x = p.x;
        rect_reg.r.y = p.y;
        rect_reg.r.width = w->width;
        rect_reg.r.height = w->height;
        rect_reg.region = w->frame_buffer;
        //draw_rect_pixels(&canvas, &rect_reg);
        repaint(rect_reg.r);
        display_recur(w->self);
    }
}

/*
 * Iterate through the whole tree and draw all window
 * This is slow! And ! It doesn't consider the case where windows overlap with each other
 * */
void display_all_window() {
    // Display super window and all its children
    // How many jiffies this take?
    window_display(get_super_window());
}

/*
* Helper function for display_all_window
*/
void display_recur(gtreenode_t * t) {
    foreach(child, t->children) {
        gtreenode_t * node = child->val;
        window_t * w = node->value;
        window_display(w);
    }
}

/*
* Getter for super window, it's esentially the desktop
*/
window_t * get_super_window() {
    return windows_tree->root->value;
}


/*
* The fill color is used when I drew window directly to the screen buffer, it's deprecated now.
*/
void set_window_fillcolor(window_t * w, uint32_t color) {
    w->fill_color = color;
}


/*
* Move window to a start at (x,y)
*/
void move_window(window_t * w, int x, int y) {
    int oldx = w->x;
    int oldy = w->y;

    w->x = x;
    w->y = y;

    repaint(rect_create(oldx, oldy, w->width, w->height));
    repaint(rect_create(w->x, w->y, w->width, w->height));
}


/*
* Minimize window
*/
void minimize_window(window_t * w) {
    w->is_minimized = 1;
    repaint(rect_create(w->x, w->y, w->width, w->height));
}


/*
* Maximize window, this doesn't work yet
*/
void maximize_window(window_t * w) {
    if(w->is_maximized == 1) {
        // Restore original size
        resize_window(w, w->original_width, w->original_height);
        w->is_maximized = 0;
    }
    else {
        // Full screen
        w->original_width = w->width;
        w->original_height = w->height;
        resize_window(w, 1024, 768);
        w->is_maximized = 1;
    }
}


/*
* Different from minimize_window, close_window actually removes the window from the window tree,
*/
void close_window(window_t * w) {
    point_t p = get_canonical_coordinates(w);
    int oldx = p.x;
    int oldy = p.y;
    int oldw = w->width;
    int oldh = w->height;
    // Remove the window from tree
    tree_remove(windows_tree, w->self);
    repaint(rect_create(oldx, oldy, oldw, oldh));
}

/*
* Resize window, this function doesn't work now after I start to give frame buffer to every window, don't use it for now
*/
void resize_window(window_t * w, int new_width, int new_height) {
    int oldw = w->width;
    int oldh = w->height;
    w->width = new_width;
    w->height = new_height;

    repaint(rect_create(w->x, w->y, oldw, oldh));
    repaint(rect_create(w->x, w->y, new_width, new_height));
}

/*
 * Copy pixels in rect to dst
 * */
void copy_rect(uint32_t * dst, rect_t r) {
    for(int i = r.y; i < r.y + r.height; i++) {
        for(int j = r.x; j < r.x + r.width; j++) {
            *dst = screen[screen_width * i + j];
            //*dst = 0x00FF00FF;
            dst++;
        }
    }
}

/*
 * Figure out which window has the point(x,y)
 * */
window_t * query_window_by_point(int x, int y) {
    // Very Important: Do not allocate any memory here, because this function is probably going to be called 99999 times per second, you don't want to call lots of mallocs in here
    // So, we will just assume that it's impossible that there are more than 20 windows stacked together and all contain one point
    window_t * possible_windows[20];

    // A list of windows that are standing on this pixel
    //list_t * possible_windows = list_create();
    int num = 0;
    find_possible_windows(x, y, windows_tree->root, possible_windows, &num);
    // Now, we get a list of possible windows, find which one is on top (whichever has the most children is the one on top!)
    int max_children_num = 0;
    window_t * top_window = NULL;

    window_t ** window_list = possible_windows;
    for(int i = 0; i < num; i++) {
        window_t * w = window_list[i];
        if(list_size(w->under_windows) >= max_children_num && w->is_minimized != 1) {
            max_children_num = list_size(w->under_windows);
            top_window = w;
        }

    }
    return top_window;
}


/*
 * Helper function for repaint(), paint a pixel given its (x,y)
 * */
void paint_pixel(canvas_t * canvas, int x, int y) {
    window_t * top_window = query_window_by_point(x, y);
    if(top_window != NULL) {
        set_pixel(canvas, get_window_pixel(top_window, x, y), x, y);
    }
}

/*
 * Given a rectangle region, repaint the pixel it should have
 * */
void repaint(rect_t r) {
    // This method is always called to repaint the whole screen, so canvas's frame buffer is always the screen
    canvas_t canvas = canvas_create(1024, 768, screen);
    for(int i = r.x; i < r.x + r.width; i++) {
        for(int j = r.y; j < r.y + r.height; j++) {
            paint_pixel(&canvas, i, j);
        }
    }
}

/*
 * A recursive function for finding windows that contains the point (x, y)
 * return value num: number of possible windows
 * */
void find_possible_windows(int x, int y, gtreenode_t * subroot, window_t ** possible_windows, int * num) {
    window_t * curr_w = subroot->value;

    if(is_point_in_window(x, y, curr_w)) {
        possible_windows[*num] = curr_w;
        (*num)++;
    }
    foreach(child, subroot->children) {
        find_possible_windows(x, y, child->val, possible_windows, num);
    }
}

/*
 * Given x,y in a global frame buffer
 * Find the corresponding pixel in the window
 * */
uint32_t get_window_pixel(window_t * w, int x, int y) {
    // Transform (x,y) so that it's relative to the windows's frame buffer
    point_t p = get_relative_coordinates(w, x, y);
    int idx = w->width * p.y + p.x;
    return w->frame_buffer[idx];
}

/*
* Is a point in the rectangle ?
*/
int is_point_in_rect(int point_x, int point_y, rect_t * r) {
    return (point_x >= r->x && point_x < r->x + r->width) && (point_y >= r->y && point_y < r->y + r->height);
}

/*
* Is a point in the window?
*/
int is_point_in_window(int x, int y, window_t * w) {
    rect_t r;
    point_t p = get_canonical_coordinates(w);
    r.x = p.x;
    r.y = p.y;
    r.width = w->width;
    r.height = w->height;
    return is_point_in_rect(x, y, &r);
}


/*
* Getter for screen canvas
*/
canvas_t * get_screen_canvas() {
    return &canvas;
}

/*
* Canonical coordinates is the coordinates relative to the whole screen
* Relative coordinates is the coordiantes relative to its parent
* This function convert canonical coords to relative
*/
point_t get_relative_coordinates(window_t * w, int x, int y) {
    point_t p = get_canonical_coordinates(w);
    p.x = x - p.x;
    p.y = y - p.y;
    return p;
}

/*
* This function convert relative coords to canonical one
*/
point_t get_canonical_coordinates(window_t * w) {
    point_t p;
    p.x = w->x;
    p.y = w->y;
    // Calculate the canonical xy coords
    window_t * runner = w->parent;
    while(runner != NULL) {
        p.x += runner->x;
        p.y += runner->y;
        runner = runner->parent;
    }
    return p;
}

/*
* Is two window overlap ?
*/
int is_window_overlap(window_t * w1, window_t * w2) {
    point_t p1 = get_canonical_coordinates(w1);
    point_t p2 = get_canonical_coordinates(w1);
    rect_t r1 = rect_create(p1.x, p1.y, w1->width, w1->height);
    rect_t r2 = rect_create(p2.x, p2.y, w2->width, w2->height);
    return is_rect_overlap(r1, r2);
}


/*
* Initialize compositor by getting info from vesa driver, creating screen cavans struct, and creating the super window and load desktop wallpaper
*/
void compositor_init() {
    // Get linear frame buffer from vesa driver
    screen = vesa_get_lfb();
    screen_width = vesa_get_resolution_x();
    screen_height = vesa_get_resolution_y();
    canvas = canvas_create(1024, 768, screen);

    // Initialize window tree to manage the windows to be created

    // Create a base window, this should be the parent of all windows(super window)
    windows_tree = tree_create();
    window_t * w = window_create(NULL, 0, 0, screen_width, screen_height, WINDOW_SUPER, "desktop");
    //window_add_headline(w, "");
    set_window_fillcolor(w, VESA_COLOR_GREEN);

#if 1
    bitmap_t * wallpaper = bitmap_create("/wallpaper.bmp");
    bitmap_to_framebuffer(wallpaper, w->frame_buffer);
#endif

#if 0
    memsetdw(w->frame_buffer, 0x00ff00ff,1024*768);
#endif

    // Default fill color is black
    fill_color =  (255 << 24) |(223 << 16) | (224 << 8) | 224;
}
