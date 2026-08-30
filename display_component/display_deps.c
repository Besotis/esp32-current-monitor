/*
 * Display-only dependency anchor.
 *
 * This component intentionally contains no runtime code. Its purpose is to
 * keep LVGL / esp_lvgl_port / esp_lcd out of the SENSOR dependency graph.
 */
void current_monitor_display_deps_anchor(void)
{
}
