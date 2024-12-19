/*
 * Stub module implementing the saved-session storage APIs for tools
 * that don't load or save sessions.
 */

#include "putty.h"

settings_w *open_settings_w(const char *sessionname, char **errmsg)
{ return NULL; }
void write_setting_s(settings_w *handle, const char *key, const char *value)
{ unreachable("where did you get a settings_w from?"); }
void write_setting_i(settings_w *handle, const char *key, int value)
{ unreachable("where did you get a settings_w from?"); }
void write_setting_fontspec(settings_w *handle, const char *name, FontSpec *fs)
{ unreachable("where did you get a settings_w from?"); }
void write_setting_filename(settings_w *handle, const char *name, Filename *fn)
{ unreachable("where did you get a settings_w from?"); }
void close_settings_w(settings_w *handle)
{ unreachable("where did you get a settings_w from?"); }

settings_r *open_settings_r(const char *sessionname)
{ return NULL; }
char *read_setting_s(settings_r *handle, const char *key)
{ return NULL; }
int read_setting_i(settings_r *handle, const char *key, int defvalue)
{ return defvalue; }
FontSpec *read_setting_fontspec(settings_r *handle, const char *name)
{ return fontspec_new_default(); }
Filename *read_setting_filename(settings_r *handle, const char *name)
{ return filename_from_str(""); }
void close_settings_r(settings_r *handle) { }

void del_settings(const char *sessionname) {}

settings_e *enum_settings_start(void)
{ return NULL; }
bool enum_settings_next(settings_e *handle, strbuf *out)
{ unreachable("where did you get a settings_e from?"); }
void enum_settings_finish(settings_e *handle)
{ unreachable("where did you get a settings_e from?"); }
