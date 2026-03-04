#include "lvgl.h"
#include <stdio.h>

// Use the copied font file. Ensure Font_Mono_Bold_30.c is compiled.
LV_FONT_DECLARE(Font_Mono_Bold_30);
// If the font is not found, fallback to default or built-in
// LV_FONT_DECLARE(Font_Mono_Bold_30);

// Configuration
#define MAX_POINTS 4096
#define LINE_WIDTH 5
#define REFRESH_RATE_MS 33 // ~30Hz

// Screen resolution (Based on main.cpp)
#define SCREEN_WIDTH 1440
#define SCREEN_HEIGHT 720

// Global variables
static lv_point_t line_points[MAX_POINTS];
static uint16_t point_cnt = 0;
static lv_obj_t * line_container; // Container for line objects
static lv_obj_t * current_line_obj = NULL;
static uint16_t current_line_start_idx = 0;
static bool is_new_stroke = true;

static lv_obj_t * label_coords;
static lv_obj_t * btn_clear;
static lv_obj_t * input_layer;

// Function prototypes
static void touch_event_cb(lv_event_t * e);
static void clear_event_cb(lv_event_t * e);
static void run_ui_test(lv_timer_t * timer);

// Clear canvas and reset points
static void clear_canvas() {
    point_cnt = 0;
    current_line_start_idx = 0;
    is_new_stroke = true;
    current_line_obj = NULL;
    
    // Clear all line objects
    lv_obj_clean(line_container);
    
    lv_label_set_text(label_coords, "X:0000 Y:0000");
    lv_obj_invalidate(lv_scr_act()); // Force full redraw
}

// Touch event handler
static void touch_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t * indev = lv_indev_get_act();
    if(!indev) return;

    if(code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        
        // Coordinate transformation is now handled in main.cpp based on EPD_ROTATION

        // Update coordinates label (requirement: real-time update)
        lv_label_set_text_fmt(label_coords, "X:%04d Y:%04d", p.x, p.y);

        // Add point to line if not full
        if(point_cnt < MAX_POINTS) {
            // Fix 2: Handle new stroke (disconnect lines)
            if(is_new_stroke) {
                // Create a new line object for this stroke
                current_line_obj = lv_line_create(line_container);
                lv_obj_set_style_line_width(current_line_obj, LINE_WIDTH, 0);
                lv_obj_set_style_line_color(current_line_obj, lv_color_white(), 0);
                lv_obj_set_style_line_rounded(current_line_obj, true, 0);
                
                current_line_start_idx = point_cnt;
                is_new_stroke = false;
                
                // Add the first point of the stroke
                line_points[point_cnt] = p;
                point_cnt++;
                
                // Set points for the new line object (count = 1, just a dot initially)
                lv_line_set_points(current_line_obj, &line_points[current_line_start_idx], 1);
            } 
            else {
                // Continue existing stroke
                // Simple distance check to avoid too many points at same location
                if(point_cnt > 0) {
                    lv_coord_t dx = p.x - line_points[point_cnt-1].x;
                    lv_coord_t dy = p.y - line_points[point_cnt-1].y;
                    if(dx*dx + dy*dy < 4) return; // Ignore small jitter (< 2px)
                }
                
                line_points[point_cnt] = p;
                point_cnt++;
                
                // Update points for current line object
                // Points pointer points to the start of this stroke in the global array
                uint16_t num_points = point_cnt - current_line_start_idx;
                lv_line_set_points(current_line_obj, &line_points[current_line_start_idx], num_points);
            }
        }
    }
    else if(code == LV_EVENT_RELEASED) {
        // Fix 2: Mark end of stroke
        is_new_stroke = true;
        current_line_obj = NULL;
    }
}

// Clear button event handler
static void clear_event_cb(lv_event_t * e) {
    clear_canvas();
}

// Main UI entry point
void ui_entry(void)
{
    lv_obj_t * scr = lv_scr_act();
    
    // 1. Set background to pure black (requirement 4)
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // 2. Create Line Container
    line_container = lv_obj_create(scr);
    lv_obj_set_size(line_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(line_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(line_container, 0, 0);
    lv_obj_clear_flag(line_container, LV_OBJ_FLAG_SCROLLABLE);

    // 3. Create transparent input layer to capture touch events
    input_layer = lv_obj_create(scr);
    lv_obj_set_size(input_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(input_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_layer, 0, 0);
    lv_obj_clear_flag(input_layer, LV_OBJ_FLAG_SCROLLABLE);
    // Listen to ALL events to catch RELEASED
    lv_obj_add_event_cb(input_layer, touch_event_cb, LV_EVENT_ALL, NULL);

    // 4. Create Clear Button (requirement 2)
    btn_clear = lv_btn_create(scr);
    lv_obj_set_size(btn_clear, 100, 50); // >= 60x40 px
    lv_obj_align(btn_clear, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_add_event_cb(btn_clear, clear_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_clear, lv_color_white(), 0); // 设置按键为白色

    lv_obj_t * btn_label = lv_label_create(btn_clear);
    lv_label_set_text(btn_label, "Clear");
    lv_obj_set_style_text_font(btn_label, &Font_Mono_Bold_30, 0); 
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0); // 设置按键文字为白色
    lv_obj_center(btn_label);

    // 5. Create Coordinates Label (requirement 3)
    label_coords = lv_label_create(scr);
    lv_label_set_text(label_coords, "X:0000 Y:0000");
    
    // Try to use the bold font, fallback to default if needed
    // Note: User must ensure Font_Mono_Bold_30.c is compiled or linked
    lv_obj_set_style_text_font(label_coords, &Font_Mono_Bold_30, 0); 
    
    lv_obj_set_style_text_color(label_coords, lv_color_white(), 0);
    // Align to the left of the Clear button
    lv_obj_align(label_coords, LV_ALIGN_TOP_LEFT, 20, 10);

    // Start unit test after 2 seconds
    // lv_timer_create(run_ui_test, 2000, NULL);
}

// Unit Test Implementation (requirement 7)
static void run_ui_test(lv_timer_t * timer) {
    static int step = 0;
    static int swipe_count = 0;
    
    if(step == 0) {
        printf("Starting Unit Test...\n");
    }

    // Simulate 10 swipes
    if(swipe_count < 10) {
        // Generate a synthetic swipe
        // Note: Simulated points need to be "raw" if we want to test transformation,
        // but here we are manipulating line_points directly for visual test.
        // Actually, let's simulate the effect on line_points directly to verify drawing logic.
        
        // Start a new stroke
        is_new_stroke = true; 
        
        // We need to manually invoke the logic or just manipulate the data structures
        // to test the "multiple lines" capability.
        
        current_line_obj = lv_line_create(line_container);
        lv_obj_set_style_line_width(current_line_obj, LINE_WIDTH, 0);
        lv_obj_set_style_line_color(current_line_obj, lv_color_white(), 0);
        
        current_line_start_idx = point_cnt;
        int start_x = 100 + swipe_count * 20;
        int start_y = 100;
        
        for(int i=0; i<20; i++) {
            if(point_cnt < MAX_POINTS) {
                line_points[point_cnt].x = start_x + i * 2;
                line_points[point_cnt].y = start_y + i * 2;
                point_cnt++;
            }
        }
        lv_line_set_points(current_line_obj, &line_points[current_line_start_idx], point_cnt - current_line_start_idx);
        
        // Update label
        if(point_cnt > 0) {
            lv_label_set_text_fmt(label_coords, "X:%04d Y:%04d", 
                line_points[point_cnt-1].x, line_points[point_cnt-1].y);
        }
        
        swipe_count++;
        printf("Simulated Swipe %d/10\n", swipe_count);
        
        // End stroke
        is_new_stroke = true;
    } 
    // Simulate Clear
    else if(swipe_count == 10) {
        printf("Simulating Clear Button Press...\n");
        clear_canvas();
        if(point_cnt == 0) {
            printf("Clear Verified: Points reset to 0\n");
        } else {
            printf("Clear Failed!\n");
        }
        swipe_count++; // Stop testing
    }
    
    step++;
    if(swipe_count > 10) {
        lv_timer_del(timer);
        printf("Unit Test Completed.\n");
    }
}
