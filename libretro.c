#include <libretro.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

#include <libspectrum.h>

#include <fuse/settings.h>
#include <fuse/utils.h>
#include <fuse/ui/ui.h>
#include <fuse/keyboard.h>

#include <keyboverlay.h>

static void dummy_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   (void)fmt;
}

#define RETRO_DEVICE_CURSOR_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_KEMPSTON_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_SINCLAIR1_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define RETRO_DEVICE_SINCLAIR2_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 3)
#define RETRO_DEVICE_TIMEX1_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 4)
#define RETRO_DEVICE_TIMEX2_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 5)
#define RETRO_DEVICE_FULLER_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 6)

#define MAX_WIDTH  320
#define MAX_HEIGHT 240

static retro_log_printf_t log_cb = dummy_log;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t env_cb;

static struct retro_frame_time_callback time_cb;
static struct retro_perf_callback perf_cb;

#define MAX_PADS 7
static unsigned input_devices[MAX_PADS];

static uint16_t image_buffer[MAX_WIDTH * MAX_HEIGHT];
static uint16_t image_buffer_2[MAX_WIDTH * MAX_HEIGHT];
static unsigned first_pixel;
static unsigned soft_width, soft_height;
static unsigned hard_width, hard_height;
static int hide_border, change_geometry;
static bool some_audio, show_frame, select_pressed, keyb_overlay;
static int keyb_transparent;
static unsigned keyb_x, keyb_y;
static int64_t keyb_send, keyb_hold_time;
static input_event_t keyb_event;
static void*  snapshot_buffer;
static size_t snapshot_size;

static const struct { unsigned x; unsigned y; } keyb_positions[4] = {
   { 32, 40 }, { 40, 88 }, { 48, 136 }, { 32, 184 }
};

static struct retro_input_descriptor input_descriptors[] = {
   // See fuse-1.1.1\peripherals\joystick.c
   // Cursor
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // Kempston
   { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // Sinclair 1
   { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // Sinclair 2
   { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // Timex 1
   { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // Timex 2
   { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // Fuller
   { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Keyboard overlay" },
   // TODO: Is this needed?
   { 255, 255, 255, 255, "" }
};

static bool input_state[MAX_PADS][5];
 
keysyms_map_t keysyms_map[] = {
   { RETROK_TAB,       INPUT_KEY_Tab         },
   { RETROK_RETURN,    INPUT_KEY_Return      },
   { RETROK_ESCAPE,    INPUT_KEY_Escape      },
   { RETROK_SPACE,     INPUT_KEY_space       },
   { RETROK_EXCLAIM,   INPUT_KEY_exclam      },
   { RETROK_HASH,      INPUT_KEY_numbersign  },
   { RETROK_DOLLAR,    INPUT_KEY_dollar      },
   { RETROK_AMPERSAND, INPUT_KEY_ampersand   },
   { RETROK_QUOTE,     INPUT_KEY_apostrophe  },
   { RETROK_LEFTPAREN, INPUT_KEY_parenleft   },
   { RETROK_RIGHTPAREN, INPUT_KEY_parenright  },
   { RETROK_ASTERISK,  INPUT_KEY_asterisk    },
   { RETROK_PLUS,      INPUT_KEY_plus        },
   { RETROK_COMMA,     INPUT_KEY_comma       },
   { RETROK_MINUS,     INPUT_KEY_minus       },
   { RETROK_PERIOD,    INPUT_KEY_period      },
   { RETROK_SLASH,     INPUT_KEY_slash       },
   { RETROK_0,         INPUT_KEY_0           },
   { RETROK_1,         INPUT_KEY_1           },
   { RETROK_2,         INPUT_KEY_2           },
   { RETROK_3,         INPUT_KEY_3           },
   { RETROK_4,         INPUT_KEY_4           },
   { RETROK_5,         INPUT_KEY_5           },
   { RETROK_6,         INPUT_KEY_6           },
   { RETROK_7,         INPUT_KEY_7           },
   { RETROK_8,         INPUT_KEY_8           },
   { RETROK_9,         INPUT_KEY_9           },
   { RETROK_COLON,     INPUT_KEY_colon       },
   { RETROK_SEMICOLON, INPUT_KEY_semicolon   },
   { RETROK_LESS,      INPUT_KEY_less        },
   { RETROK_EQUALS,    INPUT_KEY_equal       },
   { RETROK_GREATER,   INPUT_KEY_greater     },
   { RETROK_CARET,     INPUT_KEY_asciicircum },
   { RETROK_a,         INPUT_KEY_a           },
   { RETROK_b,         INPUT_KEY_b           },
   { RETROK_c,         INPUT_KEY_c           },
   { RETROK_d,         INPUT_KEY_d           },
   { RETROK_e,         INPUT_KEY_e           },
   { RETROK_f,         INPUT_KEY_f           },
   { RETROK_g,         INPUT_KEY_g           },
   { RETROK_h,         INPUT_KEY_h           },
   { RETROK_i,         INPUT_KEY_i           },
   { RETROK_j,         INPUT_KEY_j           },
   { RETROK_k,         INPUT_KEY_k           },
   { RETROK_l,         INPUT_KEY_l           },
   { RETROK_m,         INPUT_KEY_m           },
   { RETROK_n,         INPUT_KEY_n           },
   { RETROK_o,         INPUT_KEY_o           },
   { RETROK_p,         INPUT_KEY_p           },
   { RETROK_q,         INPUT_KEY_q           },
   { RETROK_r,         INPUT_KEY_r           },
   { RETROK_s,         INPUT_KEY_s           },
   { RETROK_t,         INPUT_KEY_t           },
   { RETROK_u,         INPUT_KEY_u           },
   { RETROK_v,         INPUT_KEY_v           },
   { RETROK_w,         INPUT_KEY_w           },
   { RETROK_x,         INPUT_KEY_x           },
   { RETROK_y,         INPUT_KEY_y           },
   { RETROK_z,         INPUT_KEY_z           },
   { RETROK_BACKSPACE, INPUT_KEY_BackSpace   },
   { RETROK_KP_ENTER,  INPUT_KEY_KP_Enter    },
   { RETROK_UP,        INPUT_KEY_Up          },
   { RETROK_DOWN,      INPUT_KEY_Down        },
   { RETROK_LEFT,      INPUT_KEY_Left        },
   { RETROK_RIGHT,     INPUT_KEY_Right       },
   { RETROK_INSERT,    INPUT_KEY_Insert      },
   { RETROK_DELETE,    INPUT_KEY_Delete      },
   { RETROK_HOME,      INPUT_KEY_Home        },
   { RETROK_END,       INPUT_KEY_End         },
   { RETROK_PAGEUP,    INPUT_KEY_Page_Up     },
   { RETROK_PAGEDOWN,  INPUT_KEY_Page_Down   },
   { RETROK_F1,        INPUT_KEY_F1          },
   { RETROK_F2,        INPUT_KEY_F2          },
   { RETROK_F3,        INPUT_KEY_F3          },
   { RETROK_F4,        INPUT_KEY_F4          },
   { RETROK_F5,        INPUT_KEY_F5          },
   { RETROK_F6,        INPUT_KEY_F6          },
   { RETROK_F7,        INPUT_KEY_F7          },
   { RETROK_F8,        INPUT_KEY_F8          },
   { RETROK_F9,        INPUT_KEY_F9          },
   { RETROK_F10,       INPUT_KEY_F10         },
   { RETROK_F11,       INPUT_KEY_F11         },
   { RETROK_F12,       INPUT_KEY_F12         },
   { RETROK_LSHIFT,    INPUT_KEY_Shift_L     },
   { RETROK_RSHIFT,    INPUT_KEY_Shift_R     },
   { RETROK_LCTRL,     INPUT_KEY_Control_L   },
   { RETROK_RCTRL,     INPUT_KEY_Control_R   },
   { RETROK_LALT,      INPUT_KEY_Alt_L       },
   { RETROK_RALT,      INPUT_KEY_Alt_R       },
   { RETROK_LMETA,     INPUT_KEY_Meta_L      },
   { RETROK_RMETA,     INPUT_KEY_Meta_R      },
   { RETROK_LSUPER,    INPUT_KEY_Super_L     },
   { RETROK_RSUPER,    INPUT_KEY_Super_R     },
   { RETROK_MENU,      INPUT_KEY_Mode_switch },
   { 0, 0 }			/* End marker: DO NOT MOVE! */
};

static const uint16_t palette[16] = {
   0x0000, 0x0018, 0xc000, 0xc018,
   0x0600, 0x0618, 0xc600, 0xc618,
   0x0000, 0x001f, 0xf800, 0xf81f,
   0x07e0, 0x07ff, 0xffe0, 0xffff,
};

/*
// B&W TV palette
static const uint16_t greys[16] = {
   0x0000, 0x10a2, 0x39c7, 0x4a69,
   0x738e, 0x8430, 0xad55, 0xbdf7,
   0x0000, 0x18e3, 0x4a69, 0x6b4d,
   0x94b2, 0xb596, 0xe71c, 0xffff,
};
*/

#ifdef LOG_PERFORMANCE
#define RETRO_PERFORMANCE_INIT(name)  static struct retro_perf_counter name = {#name}; if (!name.registered) perf_cb.perf_register(&(name))
#define RETRO_PERFORMANCE_START(name) perf_cb.perf_start(&(name))
#define RETRO_PERFORMANCE_STOP(name)  perf_cb.perf_stop(&(name))
#else
#define RETRO_PERFORMANCE_INIT(name)
#define RETRO_PERFORMANCE_START(name)
#define RETRO_PERFORMANCE_STOP(name)
#endif

static void update_bool(const char* key, int* value, int def)
{
   struct retro_variable var;
  
   var.key = key;
   var.value = NULL;
   int x = def;
   
   if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         x = 1;
      else if (!strcmp(var.value, "disabled"))
         x = 0;
      else
         log_cb(RETRO_LOG_ERROR, "Invalid value for %s: %s\n", key, var.value);
   }
   
   *value = x;
   log_cb(RETRO_LOG_INFO, "%s set to %d\n", key, x);
}

static void update_string_list(const char* key, const char** value, const char* options[], const char* values[], unsigned count)
{
   struct retro_variable var;
  
   var.key = key;
   var.value = NULL;
   const char* x = values[0];
   
   if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned i;
      bool found = false;
      
      for (i = 0; i < count; i++)
      {
         if (!strcmp(var.value, options[i]))
         {
            x = values[i];
            found = true;
            break;
         }
      }
      
      if (!found)
      {
         log_cb(RETRO_LOG_ERROR, "Invalid value for %s: \"%s\"\n", key, var.value);
      }
   }
   
   if (*value)
   {
      libspectrum_free(*value);
   }
   
   *value = utils_safe_strdup(x);
   log_cb(RETRO_LOG_INFO, "%s set to \"%s\"\n", key, x);
}

static void update_long_list(const char* key, long* value, long values[], unsigned count)
{
   struct retro_variable var;
  
   var.key = key;
   var.value = NULL;
   long x = values[0];
   
   if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      char *endptr;
      long y = strtol(var.value, &endptr, 10);
      
      if (*endptr != 0 || errno == ERANGE)
      {
         log_cb(RETRO_LOG_ERROR, "Invalid value for %s: %s\n", key, var.value);
      }
      else
      {
         unsigned i;
         bool found = false;
         
         for (i = 0; i < count; i++)
         {
            if (y == values[i])
            {
               x = y;
               found = true;
               break;
            }
         }
         
         if (!found)
         {
            log_cb(RETRO_LOG_ERROR, "Invalid value for %s: %s\n", key, var.value);
         }
      }
   }
   
   *value = x;
   log_cb(RETRO_LOG_INFO, "%s set to %ld\n", key, x);
}

static void update_variables()
{
   {
      // Change this when we're emulating Timex. The MAX_[WIDTH|HEIGHT] macros must be changed also.
      hard_width = 320;
      hard_height = 240;
      
      soft_width = hard_width;
      soft_height = hard_height;
      first_pixel = 0;
      
      change_geometry = true;
      
      log_cb(RETRO_LOG_INFO, "Hard resolution set to %ux%u\n", hard_width, hard_height);
      log_cb(RETRO_LOG_INFO, "Soft resolution set to %ux%u\n", soft_width, soft_height);
   }
   
   {
      int value;
      update_bool("fuse_hide_border", &value, 0);
      
      if (value != hide_border)
      {
         hide_border = value;
         
         soft_width = hide_border ? 256 : 320;
         soft_height = hide_border ? 192 : 240;
         first_pixel = ( hard_height - soft_height ) / 2 * hard_width + ( hard_width - soft_width ) / 2;
         
         change_geometry = true;
         
         log_cb(RETRO_LOG_INFO, "Soft resolution set to %ux%u\n", soft_width, soft_height);
      }
   }
   
   update_bool("fuse_fast_load", &settings_current.accelerate_loader, 1);
   settings_current.fastload = settings_current.accelerate_loader;

   update_bool("fuse_load_sound", &settings_current.sound_load, 1);

   {
      static const char* options[] = { "tv speaker", "beeper", "unfiltered" };
      static const char* values[]  = { "TV speaker", "Beeper", "Unfiltered" };
      update_string_list("fuse_speaker_type", &settings_current.speaker_type, options, values, sizeof(options) / sizeof(options[0]));
   }

   {
      static const char* options[] = { "none", "acb", "abc" };
      static const char* values[]  = { "None", "ACB", "ABC" };
      update_string_list("fuse_ay_stereo_separation", &settings_current.stereo_ay, options, values, sizeof(options) / sizeof(options[0]));
   }

   update_bool("fuse_key_ovrlay_transp", &keyb_transparent, 1);

   {
      static long values[] = { 500, 1000, 100, 300 };
      long value;
      update_long_list("fuse_key_hold_time", &value, values, sizeof(values) / sizeof(values[0]));
      keyb_hold_time = value * 1000LL;
   }
}

static int get_joystick(unsigned device)
{
   switch (device)
   {
      case RETRO_DEVICE_CURSOR_JOYSTICK:    return 1;
      case RETRO_DEVICE_KEMPSTON_JOYSTICK:  return 2;
      case RETRO_DEVICE_SINCLAIR1_JOYSTICK: return 3;
      case RETRO_DEVICE_SINCLAIR2_JOYSTICK: return 4;
      case RETRO_DEVICE_TIMEX1_JOYSTICK:    return 5;
      case RETRO_DEVICE_TIMEX2_JOYSTICK:    return 6;
      case RETRO_DEVICE_FULLER_JOYSTICK:    return 7;
   }
   
   log_cb(RETRO_LOG_ERROR, "Unknown device 0x%04x\n", device);
   return 0;
}

static int fuse_init(int argc, char **argv);
static int fuse_end(void);
int fuse_settings_init( int *first_arg, int argc, char **argv );
int fuse_ui_error_specific( ui_error_level severity, const char *message );
int tape_is_playing( void );

// Replacement function for settings_init so we can change the settings before
// the emulation starts without having to build an extensive argv.
int settings_init(int *first_arg, int argc, char **argv)
{
   int res = fuse_settings_init(first_arg, argc, argv);
  
   settings_current.auto_load = 1;
   settings_current.detect_loader = 1;
   
   settings_current.printer = 0;
   
   settings_current.bw_tv = 0;

   settings_current.sound = 1;
   settings_current.sound_force_8bit = 0;
   settings_current.sound_freq = 44100;
   settings_current.sound_load = 1;
   
   settings_current.joy_kempston = 1;
   settings_current.fuller = 1;
   settings_current.joystick_1_output = 1;
   settings_current.joystick_2_output = 2;
   
   update_variables();
   return res;
}

// This is an ugly hack so that we can have the frontend manage snapshots for us
int fuse_write_snapshot(const char *filename, const unsigned char *buffer, size_t length)
{
   log_cb(RETRO_LOG_DEBUG, "%s(\"%s\", %p, %lu)\n", __FUNCTION__, filename, buffer, length);
   
   if (snapshot_buffer)
   {
      free(snapshot_buffer);
   }
   
   snapshot_buffer = malloc(length);
   
   if (snapshot_buffer)
   {
      memcpy(snapshot_buffer, buffer, length);
      snapshot_size = length;
      return 0;
   }
   
   return 1;
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = PACKAGE_NAME;
   info->library_version = PACKAGE_VERSION;
   info->need_fullpath = true;
   info->block_extract = false;
   info->valid_extensions = "tzx|tap|z80";
}

void retro_set_environment(retro_environment_t cb)
{
   env_cb = cb;

   static const struct retro_variable vars[] = {
      { "fuse_hide_border", "Hide Video Border; disabled|enabled" },
      { "fuse_fast_load", "Tape Fast Load; enabled|disabled" },
      { "fuse_load_sound", "Tape Load Sound; enabled|disabled" },
      { "fuse_speaker_type", "Speaker Type; tv speaker|beeper|unfiltered" },
      { "fuse_ay_stereo_separation", "AY Stereo Separation; none|acb|abc" },
      { "fuse_key_ovrlay_transp", "Transparent Keyboard Overlay; enabled|disabled" },
      { "fuse_key_hold_time", "Time to Release Key in ms; 500|1000|100|300" },
      { NULL, NULL },
   };
   
   static const struct retro_controller_description controllers[] = {
      { "Cursor Joystick", RETRO_DEVICE_CURSOR_JOYSTICK },
      { "Kempston Joystick", RETRO_DEVICE_KEMPSTON_JOYSTICK },
      { "Sinclair 1 Joystick", RETRO_DEVICE_SINCLAIR1_JOYSTICK },
      { "Sinclair 2 Joystick", RETRO_DEVICE_SINCLAIR2_JOYSTICK },
      { "Timex 1 Joystick", RETRO_DEVICE_TIMEX1_JOYSTICK },
      { "Timex 2 Joystick", RETRO_DEVICE_TIMEX2_JOYSTICK },
      { "Fuller Joystick", RETRO_DEVICE_FULLER_JOYSTICK }
   };
   
   static const struct retro_controller_info ports[] = {
      { controllers, 7 },
      { controllers, 7 },
      { controllers, 7 },
      { controllers, 7 },
      { controllers, 7 },
      { controllers, 7 },
      { controllers, 7 },
      { 0 }
   };

   // This seem to be broken right now, as info is NULL in retro_load_game
   // even when we load a game via the frontend after booting to BASIC.
   //bool _true = true;
   //cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, (void*)&_true);
   
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_init(void)
{
   struct retro_log_callback log;

   if (env_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
   {
      log_cb = log.log;
   }

   // Always get the perf interface because we need it in compat_timer_get_time
   // and compat_timer_sleep.
   if (!env_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
   {
      perf_cb.get_time_usec = NULL;
   }
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (!perf_cb.get_time_usec)
   {
      log_cb(RETRO_LOG_ERROR, "Fuse needs the perf interface");
      return false;
   }
   
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_ERROR, "RGB565 is not supported\n");
      return false;
   }
   
   env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_descriptors);
   memset(input_state, 0, sizeof(input_state));
   select_pressed = keyb_overlay = false;
   keyb_x = keyb_y = 0;
   keyb_send = 0;
   snapshot_buffer = NULL;
   
   if (info != NULL)
   {
      char tape[PATH_MAX];
      snprintf(tape, sizeof(tape), "-t%s", info->path);
      tape[sizeof(tape) - 1] = 0;
      
      char *argv[] = {
         "fuse",
         tape
      };
      
      log_cb(RETRO_LOG_INFO, "Loading %s\n", info->path);
      return fuse_init(sizeof(argv) / sizeof(argv[0]), argv) == 0;
   }
   
   // Boots the emulation into BASIC
   char *argv[] = {
      "fuse"
   };
   
   log_cb(RETRO_LOG_INFO, "Booting into BASIC\n");
   return fuse_init(sizeof(argv) / sizeof(argv[0]), argv) == 0;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   (void)cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   log_cb(RETRO_LOG_DEBUG, "%s(%p)\n", __FUNCTION__, info);
   
   // Here we always use the "hard" resolution to accomodate output with *and*
   // without the video border
   info->geometry.base_width = hard_width;
   info->geometry.base_height = hard_height;
   
   info->geometry.max_width = MAX_WIDTH;
   info->geometry.max_height = MAX_HEIGHT;
   info->geometry.aspect_ratio = 0.0f;
   info->timing.fps = 50.0;
   info->timing.sample_rate = 44100.0;
   
   log_cb(RETRO_LOG_INFO, "Set retro_system_av_info to:\n");
   log_cb(RETRO_LOG_INFO, "  base_width   = %u\n", info->geometry.base_width);
   log_cb(RETRO_LOG_INFO, "  base_height  = %u\n", info->geometry.base_height);
   log_cb(RETRO_LOG_INFO, "  max_width    = %u\n", info->geometry.max_width);
   log_cb(RETRO_LOG_INFO, "  max_height   = %u\n", info->geometry.max_height);
   log_cb(RETRO_LOG_INFO, "  aspect_ratio = %f\n", info->geometry.max_height);
   log_cb(RETRO_LOG_INFO, "  fps          = %f\n", info->timing.fps);
   log_cb(RETRO_LOG_INFO, "  sample_rate  = %f\n", info->timing.sample_rate);
}

static void render_video(void)
{
   if (change_geometry)
   {
      struct retro_game_geometry geometry;
      
      // Here we use the "soft" resolution that is changed according to the
      // fuse_hide_border variable
      geometry.base_width = soft_width;
      geometry.base_height = soft_height;
      
      geometry.max_width = MAX_WIDTH;
      geometry.max_height = MAX_HEIGHT;
      geometry.aspect_ratio = 0.0f;
      
      env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
      change_geometry = false;
      
      log_cb(RETRO_LOG_INFO, "Set retro_game_geometry to:\n");
      log_cb(RETRO_LOG_INFO, "  base_width   = %u\n", geometry.base_width);
      log_cb(RETRO_LOG_INFO, "  base_height  = %u\n", geometry.base_height);
      log_cb(RETRO_LOG_INFO, "  max_width    = %u\n", geometry.max_width);
      log_cb(RETRO_LOG_INFO, "  max_height   = %u\n", geometry.max_height);
      log_cb(RETRO_LOG_INFO, "  aspect_ratio = %f\n", geometry.max_height);
   }
   
   if (!keyb_overlay)
   {
      video_cb(show_frame ? image_buffer + first_pixel : NULL, soft_width, soft_height, hard_width * sizeof(uint16_t));
   }
   else
   {
      if (show_frame)
      {
         if (keyb_transparent)
         {
            uint16_t* src1 = keyboard_overlay;
            uint16_t* src2 = image_buffer;
            uint16_t* end = src1 + sizeof(keyboard_overlay) / sizeof(keyboard_overlay[0]);
            uint16_t* dest = image_buffer_2;
         
            do
            {
               uint32_t src1_pixel = *src1++ & 0xe79c;
               uint32_t src2_pixel = *src2++ & 0xe79c;
               
               *dest++ = (src1_pixel * 3 + src2_pixel) >> 2;
            }
            while (src1 < end);
         }
         else
         {
            memcpy(image_buffer_2, keyboard_overlay, sizeof(keyboard_overlay));
         }
         
         unsigned x = keyb_positions[keyb_y].x + keyb_x * 24;
         unsigned y = keyb_positions[keyb_y].y;
         unsigned width = 23;
         
         if (keyb_y == 3)
         {
            if (keyb_x == 8)
            {
               width = 24;
            }
            else if (keyb_x == 9)
            {
               x++;
               width = 30;
            }
         }
         
         uint16_t* pixel = image_buffer_2 + y * 320 + x + 1;
         unsigned i, j;
         
         for (i = 0; i < width - 2; i++)
         {
            *pixel = ~*pixel;
            pixel++;
         }
         
         pixel += 320 - width + 1;
         
         for (j = 0; j < 22; j++)
         {
            for (i = 0; i < width; i++)
            {
               *pixel = ~*pixel;
               pixel++;
            }
            
            pixel += 320 - width;
         }
         
         for (i = 0; i < width - 2; i++)
         {
            pixel++;
            *pixel = ~*pixel;
         }
         
         video_cb(image_buffer_2 + first_pixel, soft_width, soft_height, hard_width * sizeof(uint16_t));
      }
      else
      {
         video_cb(NULL, soft_width, soft_height, hard_width * sizeof(uint16_t));
      }
   }
}

void retro_run(void)
{
   // TODO: Can (should) we call the video callback inside the loops?
   bool updated = false;
   
   if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      update_variables();
   }

   show_frame = some_audio = false;
   
   if (settings_current.fastload && tape_is_playing())
   {
      // Repeat the loop while the tape is playing if we're fast loading.
      do {
         input_poll_cb();
         z80_do_opcodes();
         event_do_events();
      }
      while (tape_is_playing());
   }
   else
   {
      /*
      After playing Sabre Wulf's initial title music, fuse starts generating
      audio only for every other frame. RetroArch computes the FPS based on
      this and tries to call retro_run at double the rate to compensate for
      the audio. When vsync is on, FPS will cap at some value and the game
      will run slower.
      
      This solution guarantees that every call to retro_run generates audio, so
      emulation runs steadly at 50 FPS. Ideally, we should investigate why fuse
      is doing that and fix it, but this solutions seems to work just fine.
      */
      do {
         input_poll_cb();
         z80_do_opcodes();
         event_do_events();
      }
      while (!some_audio);
   }
   
   render_video();
}

void retro_deinit(void)
{
   fuse_end();

#ifdef LOG_PERFORMANCE
   perf_cb.perf_log();
#endif
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   switch (device)
   {
      case RETRO_DEVICE_CURSOR_JOYSTICK:
      case RETRO_DEVICE_KEMPSTON_JOYSTICK:
      case RETRO_DEVICE_SINCLAIR1_JOYSTICK:
      case RETRO_DEVICE_SINCLAIR2_JOYSTICK:
      case RETRO_DEVICE_TIMEX1_JOYSTICK:
      case RETRO_DEVICE_TIMEX2_JOYSTICK:
      case RETRO_DEVICE_FULLER_JOYSTICK:
         input_devices[port] = device;
         
         if (port == 0)
         {
            settings_current.joystick_1_output = get_joystick(device);
            log_cb(RETRO_LOG_INFO, "Joystick 1 set to 0x%04x\n", device);
         }
         else if (port == 1)
         {
            settings_current.joystick_2_output = get_joystick(device);
            log_cb(RETRO_LOG_INFO, "Joystick 2 set to 0x%04x\n", device);
         }
         
         break;
         
      default:
         log_cb(RETRO_LOG_ERROR, "Unknown device 0x%04x, setting type to RETRO_DEVICE_KEYBOARD\n", device);
         input_devices[port] = RETRO_DEVICE_KEYBOARD;
         break;
   }
}

void retro_reset(void)
{
}

size_t retro_serialize_size(void)
{
   log_cb(RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__);
   fuse_emulation_pause();
   snapshot_write("dummy.szx"); // filename is only used to get the snapshot type
   fuse_emulation_unpause();
   return snapshot_size;
}

bool retro_serialize(void *data, size_t size)
{
   log_cb(RETRO_LOG_DEBUG, "%s(%p, %lu)\n", __FUNCTION__, data, size);
   bool res = false;
   
   if (size >= snapshot_size)
   {
      memcpy(data, snapshot_buffer, snapshot_size);
      res = true;
   }
   else
   {
      log_cb(RETRO_LOG_ERROR, "Provided buffer size of %lu is less than the required size of %lu\n", size, snapshot_size);
   }

   return res;
}

bool retro_unserialize(const void *data, size_t size)
{
   log_cb(RETRO_LOG_DEBUG, "%s(%p, %lu)\n", __FUNCTION__, data, size);
   return snapshot_read_buffer(data, size, LIBSPECTRUM_ID_SNAPSHOT_SZX) == 0;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned a, bool b, const char *c)
{
   (void)a;
   (void)b;
   (void)c;
}

bool retro_load_game_special(unsigned a, const struct retro_game_info *b, size_t c)
{
   (void)a;
   (void)b;
   (void)c;
   return false;
}

void retro_unload_game(void)
{
   if (snapshot_buffer)
   {
      free(snapshot_buffer);
   }
}

unsigned retro_get_region(void)
{
   // TODO set this accordingly to the machine being emulated
   return RETRO_REGION_NTSC;
}

// Dummy callbacks for the UI

#define MENU_CALLBACK( name ) void name( int action )
MENU_CALLBACK(menu_file_savescreenaspng) {}
MENU_CALLBACK(menu_file_loadbinarydata)  {}
MENU_CALLBACK(menu_file_savebinarydata)  {}
MENU_CALLBACK(menu_machine_pause)        {}

// Dummy mkstemp

int mkstemp(char *template)
{
  return -1;
}

// Compatibility timer functions (timer.c)

double compat_timer_get_time( void )
{
   return perf_cb.get_time_usec() / 1000000.0;;
}

void compat_timer_sleep(int ms)
{
   retro_time_t t0 = perf_cb.get_time_usec();
   retro_time_t t1;
  
   /* Busy wait! */
   do {
     t1 = perf_cb.get_time_usec();
   }
   while ( ( t1 - t0 ) / 1000 < ms );
}

// Compatibility sound functions (sound.c)

int sound_lowlevel_init(const char *device, int *freqptr, int *stereoptr)
{
   (void)device;
   *freqptr = 44100;
   *stereoptr = 1;
   return 0;
}

void sound_lowlevel_end(void)
{
}

void sound_lowlevel_frame(libspectrum_signed_word *data, int len)
{
   audio_cb( data, (size_t)len / 2 );
   some_audio = true;
}

// Compatibility UI functions (ui.c)

int ui_init(int *argc, char ***argv)
{
   (void)argc;
   (void)argv;
   return 0;
}

static input_key translate(unsigned index)
{
   switch (index)
   {
      case RETRO_DEVICE_ID_JOYPAD_UP:    return INPUT_JOYSTICK_UP;
      case RETRO_DEVICE_ID_JOYPAD_DOWN:  return INPUT_JOYSTICK_DOWN;
      case RETRO_DEVICE_ID_JOYPAD_LEFT:  return INPUT_JOYSTICK_LEFT;
      case RETRO_DEVICE_ID_JOYPAD_RIGHT: return INPUT_JOYSTICK_RIGHT;
      case RETRO_DEVICE_ID_JOYPAD_A:     return INPUT_JOYSTICK_FIRE_1;
   }
   
   return INPUT_KEY_NONE;
}

int ui_event(void)
{
   static const unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_A
   };
   
   static const input_key keyb_layout[4][10] = {
      {
         INPUT_KEY_1, INPUT_KEY_2, INPUT_KEY_3, INPUT_KEY_4, INPUT_KEY_5,
         INPUT_KEY_6, INPUT_KEY_7, INPUT_KEY_8, INPUT_KEY_9, INPUT_KEY_0
      },
      {
         INPUT_KEY_Q, INPUT_KEY_W, INPUT_KEY_E, INPUT_KEY_R, INPUT_KEY_T,
         INPUT_KEY_Y, INPUT_KEY_U, INPUT_KEY_I, INPUT_KEY_O, INPUT_KEY_P
      },
      {
         INPUT_KEY_A, INPUT_KEY_S, INPUT_KEY_D, INPUT_KEY_F, INPUT_KEY_G,
         INPUT_KEY_H, INPUT_KEY_J, INPUT_KEY_K, INPUT_KEY_L, INPUT_KEY_Return
      },
      {
         INPUT_KEY_Shift_L, INPUT_KEY_Z, INPUT_KEY_X, INPUT_KEY_C, INPUT_KEY_V,
         INPUT_KEY_B, INPUT_KEY_N, INPUT_KEY_M, INPUT_KEY_Shift_R, INPUT_KEY_space
      }
   };

   //log_cb( RETRO_LOG_DEBUG, "%s\n", __FUNCTION__ );
   
   if (keyb_send != 0 && perf_cb.get_time_usec() >= keyb_send)
   {
       keyb_event.type = INPUT_EVENT_KEYRELEASE;
       input_event(&keyb_event);
       keyb_send = 0;
   }

   int16_t is_down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   
   if (is_down)
   {
      if (!select_pressed)
      {
         select_pressed = true;
         keyb_overlay = !keyb_overlay;
      }
   }
   else
   {
      select_pressed = false;
   }
   
   if (!keyb_overlay)
   {
      unsigned port, id;
      input_event_t fuse_event;
      
      for (port = 0; port < 2; port++)
      {
         for (id = 0; id < sizeof(map) / sizeof(map[0]); id++)
         {
            is_down = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, map[id]);
            
            if (is_down)
            {
               if (!input_state[port][id])
               {
                  input_state[port][id] = true;
                  input_key button = translate(map[id]);
              
                  if (button != INPUT_KEY_NONE)
                  {
                     fuse_event.type = INPUT_EVENT_JOYSTICK_PRESS;
                     fuse_event.types.joystick.which = port;
                     fuse_event.types.joystick.button = button;
              
                     input_event(&fuse_event);
                  }
               }
            }
            else
            {
               if (input_state[port][id])
               {
                  input_state[port][id] = false;
                  input_key button = translate(map[id]);
              
                  if (button != INPUT_KEY_NONE)
                  {
                     fuse_event.type = INPUT_EVENT_JOYSTICK_RELEASE;
                     fuse_event.types.joystick.which = port;
                     fuse_event.types.joystick.button = button;
              
                     input_event(&fuse_event);
                  }
               }
            }
         }
      }
   }
   else
   {
      unsigned id;
      
      for (id = 0; id < sizeof(map) / sizeof(map[0]); id++)
      {
         is_down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[id]);
         
         if (is_down)
         {
            if (!input_state[0][id])
            {
               input_state[0][id] = true;
               
               switch (map[id])
               {
                  case RETRO_DEVICE_ID_JOYPAD_UP:    keyb_y = (keyb_y - 1) & 3; break;
                  case RETRO_DEVICE_ID_JOYPAD_DOWN:  keyb_y = (keyb_y + 1) & 3; break;
                  case RETRO_DEVICE_ID_JOYPAD_LEFT:  keyb_x = keyb_x == 0 ? 9 : keyb_x - 1; break;
                  case RETRO_DEVICE_ID_JOYPAD_RIGHT: keyb_x = keyb_x == 9 ? 0 : keyb_x + 1; break;
                  case RETRO_DEVICE_ID_JOYPAD_A:
                     {
                        keyb_overlay = false;
                        
                        keyb_event.type = INPUT_EVENT_KEYPRESS;
                        keyb_event.types.key.native_key = keyb_layout[keyb_y][keyb_x];
                        keyb_event.types.key.spectrum_key = keyb_layout[keyb_y][keyb_x];
                        input_event(&keyb_event);
                        
                        keyb_send = perf_cb.get_time_usec() + keyb_hold_time;
                        return 0;
                     }
               }
            }
         }
         else
         {
            if (input_state[0][id])
            {
               input_state[0][id] = false;
            }
         }
      }
   }
   
   return 0;
}

int ui_error_specific(ui_error_level severity, const char *message)
{
   switch (severity)
   {
   case UI_ERROR_INFO:    log_cb(RETRO_LOG_INFO, "%s\n", message); break;
   case UI_ERROR_WARNING: log_cb(RETRO_LOG_WARN, "%s\n", message); break;
   case UI_ERROR_ERROR:   log_cb(RETRO_LOG_ERROR, "%s\n", message); break;
   }
  
   return fuse_ui_error_specific(severity, message);
}

int ui_end(void)
{
   return 0;
}

// Compatibility display funcions (display.c)

int uidisplay_init(int width, int height)
{
   log_cb(RETRO_LOG_DEBUG, "%s(%d, %d)\n", __FUNCTION__, width, height);
   
   if (width != 320)
   {
      log_cb(RETRO_LOG_ERROR, "Invalid value for the display width: %d\n", width);
      width = 320;
   }
   
   if (height != 240)
   {
      log_cb(RETRO_LOG_ERROR, "Invalid value for the display height: %d\n", height);
      height = 240;
   }
   
   //soft_width = (unsigned)width;
   //soft_height = (unsigned)height;
   //change_geometry = true;
   //log_cb(RETRO_LOG_INFO, "Display set to %dx%d\n", width, height);
   
   return 0;
}

void uidisplay_area(int x, int y, int w, int h)
{
   (void)x;
   (void)y;
   (void)w;
   (void)h;
}

void uidisplay_frame_end(void)
{
   show_frame = true;
}

int uidisplay_hotswap_gfx_mode(void)
{
   return 0;
}

int uidisplay_end(void)
{
   return 0;
}

void uidisplay_putpixel(int x, int y, int color)
{
   uint16_t palette_color = palette[color];
   
   if (machine_current->timex)
   {
      x <<= 1; y <<= 1;
      uint16_t* image_buffer_pos = image_buffer + (y * hard_width + x);
      
      *image_buffer_pos++ = palette_color;
      *image_buffer_pos   = palette_color;
      
      image_buffer_pos += hard_width - 1;
      
      *image_buffer_pos++ = palette_color;
      *image_buffer_pos   = palette_color;
   }
   else
   {
      uint16_t* image_buffer_pos = image_buffer + (y * hard_width + x);
      *image_buffer_pos = palette_color;
   }
}

void uidisplay_plot8(int x, int y, libspectrum_byte data, libspectrum_byte ink, libspectrum_byte paper)
{
   // TODO: SSE versions maybe?
   uint16_t palette_ink = palette[ink];
   uint16_t palette_paper = palette[paper];
   
   x <<= 3;

   if (machine_current->timex)
   {
      int i;

      x <<= 1; y <<= 1;
      uint16_t* image_buffer_pos = image_buffer + (y * hard_width + x);
      
      uint16_t data_80 = data & 0x80 ? palette_ink : palette_paper;
      uint16_t data_40 = data & 0x40 ? palette_ink : palette_paper;
      uint16_t data_20 = data & 0x20 ? palette_ink : palette_paper;
      uint16_t data_10 = data & 0x10 ? palette_ink : palette_paper;
      uint16_t data_08 = data & 0x08 ? palette_ink : palette_paper;
      uint16_t data_04 = data & 0x04 ? palette_ink : palette_paper;
      uint16_t data_02 = data & 0x02 ? palette_ink : palette_paper;
      uint16_t data_01 = data & 0x01 ? palette_ink : palette_paper;
    
      *image_buffer_pos++ = data_80;
      *image_buffer_pos++ = data_80;
      *image_buffer_pos++ = data_40;
      *image_buffer_pos++ = data_40;
      *image_buffer_pos++ = data_20;
      *image_buffer_pos++ = data_20;
      *image_buffer_pos++ = data_10;
      *image_buffer_pos++ = data_10;
      *image_buffer_pos++ = data_08;
      *image_buffer_pos++ = data_08;
      *image_buffer_pos++ = data_04;
      *image_buffer_pos++ = data_04;
      *image_buffer_pos++ = data_02;
      *image_buffer_pos++ = data_02;
      *image_buffer_pos++ = data_01;
      *image_buffer_pos   = data_01;
    
      image_buffer_pos += hard_width - 15;

      *image_buffer_pos++ = data_80;
      *image_buffer_pos++ = data_80;
      *image_buffer_pos++ = data_40;
      *image_buffer_pos++ = data_40;
      *image_buffer_pos++ = data_20;
      *image_buffer_pos++ = data_20;
      *image_buffer_pos++ = data_10;
      *image_buffer_pos++ = data_10;
      *image_buffer_pos++ = data_08;
      *image_buffer_pos++ = data_08;
      *image_buffer_pos++ = data_04;
      *image_buffer_pos++ = data_04;
      *image_buffer_pos++ = data_02;
      *image_buffer_pos++ = data_02;
      *image_buffer_pos++ = data_01;
      *image_buffer_pos   = data_01;
   }
   else
   {
      uint16_t* image_buffer_pos = image_buffer + (y * hard_width + x);
      
      *image_buffer_pos++ = data & 0x80 ? palette_ink : palette_paper;
      *image_buffer_pos++ = data & 0x40 ? palette_ink : palette_paper;
      *image_buffer_pos++ = data & 0x20 ? palette_ink : palette_paper;
      *image_buffer_pos++ = data & 0x10 ? palette_ink : palette_paper;
      *image_buffer_pos++ = data & 0x08 ? palette_ink : palette_paper;
      *image_buffer_pos++ = data & 0x04 ? palette_ink : palette_paper;
      *image_buffer_pos++ = data & 0x02 ? palette_ink : palette_paper;
      *image_buffer_pos   = data & 0x01 ? palette_ink : palette_paper;
   }
}

void uidisplay_plot16(int x, int y, libspectrum_word data, libspectrum_byte ink, libspectrum_byte paper)
{
   // TODO: SSE versions maybe?
   uint16_t palette_ink = palette[ink];
   uint16_t palette_paper = palette[paper];
   x <<= 4; y <<= 1;
   uint16_t* image_buffer_pos = image_buffer + (y * hard_width + x);
   
   uint16_t data_8000 = data & 0x8000 ? palette_ink : palette_paper;
   uint16_t data_4000 = data & 0x4000 ? palette_ink : palette_paper;
   uint16_t data_2000 = data & 0x2000 ? palette_ink : palette_paper;
   uint16_t data_1000 = data & 0x1000 ? palette_ink : palette_paper;
   uint16_t data_0800 = data & 0x0800 ? palette_ink : palette_paper;
   uint16_t data_0400 = data & 0x0400 ? palette_ink : palette_paper;
   uint16_t data_0200 = data & 0x0200 ? palette_ink : palette_paper;
   uint16_t data_0100 = data & 0x0100 ? palette_ink : palette_paper;
   uint16_t data_0080 = data & 0x0080 ? palette_ink : palette_paper;
   uint16_t data_0040 = data & 0x0040 ? palette_ink : palette_paper;
   uint16_t data_0020 = data & 0x0020 ? palette_ink : palette_paper;
   uint16_t data_0010 = data & 0x0010 ? palette_ink : palette_paper;
   uint16_t data_0008 = data & 0x0008 ? palette_ink : palette_paper;
   uint16_t data_0004 = data & 0x0004 ? palette_ink : palette_paper;
   uint16_t data_0002 = data & 0x0002 ? palette_ink : palette_paper;
   uint16_t data_0001 = data & 0x0001 ? palette_ink : palette_paper;

   *image_buffer_pos++ = data_8000;
   *image_buffer_pos++ = data_4000;
   *image_buffer_pos++ = data_2000;
   *image_buffer_pos++ = data_1000;
   *image_buffer_pos++ = data_0800;
   *image_buffer_pos++ = data_0400;
   *image_buffer_pos++ = data_0200;
   *image_buffer_pos++ = data_0100;
   *image_buffer_pos++ = data_0080;
   *image_buffer_pos++ = data_0040;
   *image_buffer_pos++ = data_0020;
   *image_buffer_pos++ = data_0010;
   *image_buffer_pos++ = data_0008;
   *image_buffer_pos++ = data_0004;
   *image_buffer_pos++ = data_0002;
   *image_buffer_pos   = data_0001;
  
   image_buffer_pos += hard_width - 15;

   *image_buffer_pos++ = data_8000;
   *image_buffer_pos++ = data_4000;
   *image_buffer_pos++ = data_2000;
   *image_buffer_pos++ = data_1000;
   *image_buffer_pos++ = data_0800;
   *image_buffer_pos++ = data_0400;
   *image_buffer_pos++ = data_0200;
   *image_buffer_pos++ = data_0100;
   *image_buffer_pos++ = data_0080;
   *image_buffer_pos++ = data_0040;
   *image_buffer_pos++ = data_0020;
   *image_buffer_pos++ = data_0010;
   *image_buffer_pos++ = data_0008;
   *image_buffer_pos++ = data_0004;
   *image_buffer_pos++ = data_0002;
   *image_buffer_pos   = data_0001;
}

void uidisplay_frame_save( void )
{
   /* FIXME: Save current framebuffer state as the widget UI wants to scribble
      in here */
}

void uidisplay_frame_restore( void )
{
   /* FIXME: Restore saved framebuffer state as the widget UI wants to draw a
      new menu */
}

// Compatibility osname function

int compat_osname(char *buffer, size_t length)
{
   strncpy(buffer, "libretro", length);
   buffer[length - 1] = 0;
   return 0;
}

// Compatibility file functions

#include <fuse/roms/48.rom.h>
#include <fuse/lib/compressed/tape_48.szx.h>

typedef struct
{
   const char *name;
   const unsigned char* ptr;
   size_t size;
}
entry_t;

static const entry_t mem_entries[] = {
   {
      FUSE_DIR_SEP_STR "fuse" FUSE_DIR_SEP_STR "roms" FUSE_DIR_SEP_STR "48.rom",
      fuse_roms_48_rom, sizeof(fuse_roms_48_rom)
   },
   {
      FUSE_DIR_SEP_STR "fuse" FUSE_DIR_SEP_STR "lib" FUSE_DIR_SEP_STR "tape_48.szx",
      fuse_lib_compressed_tape_48_szx, sizeof(fuse_lib_compressed_tape_48_szx)
   }
};

static const entry_t* find_entry(const char *path)
{
   int i;
   size_t len = strlen(path);
   
   for (i = 0; i < sizeof(mem_entries) / sizeof(mem_entries[0]); i++)
   {
      size_t len2 = strlen(mem_entries[i].name);
      
      if (!strcmp(path + len - len2, mem_entries[i].name))
      {
         return mem_entries + i;
      }
   }
   
   return NULL;
}

typedef struct
{
   int type; // 0=FILE 1=memory
   
   union {
      FILE* file;
      
      struct {
         const char* ptr;
         size_t length, remain;
      };
   };
}
compat_fd_internal;

const compat_fd COMPAT_FILE_OPEN_FAILED = NULL;

compat_fd compat_file_open(const char *path, int write)
{
   compat_fd_internal *fd = (compat_fd_internal*)malloc(sizeof(compat_fd_internal));
   
   if (fd)
   {
      if (!write)
      {
         const entry_t* entry = find_entry(path);
         
         if (entry != NULL)
         {
            fd->type = 1;
            fd->ptr = entry->ptr;
            fd->length = fd->remain = entry->size;
            
            log_cb(RETRO_LOG_INFO, "Opened \"%s\" from memory\n", path);
            return (compat_fd)fd;
         }
      }
      
      fd->type = 0;
      fd->file = fopen( path, write ? "wb" : "rb" );
      
      if (fd->file)
      {
         log_cb(RETRO_LOG_INFO, "Opened \"%s\" from the file system\n", path);
         return (compat_fd)fd;
      }
      
      free(fd);
   }
   
   return COMPAT_FILE_OPEN_FAILED;
}

off_t compat_file_get_length(compat_fd cfd)
{
   struct stat file_info;
   compat_fd_internal *fd = (compat_fd_internal*)cfd;
   
   if (fd->type == 1)
   {
      return (off_t)fd->length;
   }
 
   if (fstat(fileno(fd->file), &file_info))
   {
      ui_error(UI_ERROR_ERROR, "couldn't stat file: %s", strerror(errno));
      return -1;
   }
 
   return file_info.st_size;
}

int compat_file_read(compat_fd cfd, utils_file *file)
{
   compat_fd_internal *fd = (compat_fd_internal*)cfd;
   size_t numread;
   
   if (fd->type == 1)
   {
      numread = file->length < fd->remain ? file->length : fd->remain;
      memcpy(file->buffer, fd->ptr, numread);
      fd->ptr += numread;
      fd->remain -= numread;
   }
   else
   {
      numread = fread(file->buffer, 1, file->length, fd->file);
   }
   
   if (numread == file->length)
   {
      return 0;
   }
   
   ui_error( UI_ERROR_ERROR,
             "error reading file: expected %lu bytes, but read only %lu",
             (unsigned long)file->length, (unsigned long)numread );
   return 1;
}

int compat_file_write(compat_fd cfd, const unsigned char *buffer, size_t length)
{
   compat_fd_internal *fd = (compat_fd_internal*)cfd;
   
   size_t numwritten = fwrite(buffer, 1, length, fd->file);
   
   if (numwritten == length)
   {
      return 0;
   }
   
   ui_error( UI_ERROR_ERROR,
             "error writing file: expected %lu bytes, but wrote only %lu",
             (unsigned long)length, (unsigned long)numwritten );
   return 1;
}

int compat_file_close(compat_fd cfd)
{
   compat_fd_internal *fd = (compat_fd_internal*)cfd;
   
   if (fd->type == 0)
   {
      int res = fclose(fd->file);
      free(fd);
      return res;
   }
   
   free(fd);
   return 0;
}

int compat_file_exists(const char *path)
{
   log_cb(RETRO_LOG_INFO, "Checking if \"%s\" exists\n", path);
   return find_entry(path) != NULL || access( path, R_OK ) != -1;
}

// Compatibility path functions

const char *compat_get_temp_path(void)
{
   return ".";
}

const char *compat_get_home_path(void)
{
   return ".";
}

int
compat_is_absolute_path(const char *path)
{
   // TODO how to handle other architectures?
#ifdef WIN32
   if( path[0] == '\\' ) return 1;
   if( path[0] && path[1] == ':' ) return 1;
#else
   if ( path[0] == '/' ) return 1;
#endif

   return 0;
}

int compat_get_next_path(path_context *ctx)
{
   const char *content, *path_segment;
   
   if (!env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &content) || !content)
   {
      ui_error(UI_ERROR_ERROR, "Could not get libretro's system directory" );
      return 0;
   }

   switch (ctx->type)
   {
      case UTILS_AUXILIARY_LIB:    path_segment = "lib"; break;
      case UTILS_AUXILIARY_ROM:    path_segment = "roms"; break;
      case UTILS_AUXILIARY_WIDGET: path_segment = "ui/widget"; break;
      case UTILS_AUXILIARY_GTK:    path_segment = "ui/gtk"; break;
      
      default:
         ui_error(UI_ERROR_ERROR, "Unknown auxiliary file type %d", ctx->type);
         return 0;
   }

   switch (ctx->state++)
   {
      case 0:
         snprintf(ctx->path, PATH_MAX, "%s" FUSE_DIR_SEP_STR "fuse" FUSE_DIR_SEP_STR "%s", content, path_segment);
         ctx->path[PATH_MAX - 1] = 0;
         log_cb(RETRO_LOG_INFO, "Returning \"%s\" as path for %s\n", ctx->path, ctx->type == UTILS_AUXILIARY_LIB ? "lib" : ctx->type == UTILS_AUXILIARY_ROM ? "rom" : ctx->type == UTILS_AUXILIARY_WIDGET ? "widget" : "gtk" );
         return 1;

      case 1:
         return 0;
   }

   ui_error(UI_ERROR_ERROR, "unknown path_context state %d", ctx->state);
   return 0;
}

// Compatibility keyboard functions (keyboard.c)

int ui_keyboard_init(void)
{
   return 0;
}

void ui_keyboard_end(void)
{
}

// Compatibility joystick functions (joystick.c)

int ui_joystick_init(void)
{
   return 0;
}

void ui_joystick_end(void)
{
}

void ui_joystick_poll(void)
{
}

// Compatibility mouse functions (mouse.c)

int ui_mouse_grab(int startup)
{
   (void)startup;
   return 1;
}

int ui_mouse_release(int suspend)
{
   return !suspend;
}

// Include fuse.c source so we can call fuse_init

#include <fuse/fuse.c>

// Rename settings_init so we can extend it

#define settings_init fuse_settings_init
#include <fuse/settings.c>
#undef settings_init

// Rename ui_error_specific so we can extend it

#define ui_error_specific fuse_ui_error_specific
#include <fuse/ui/widget/error.c>
#undef ui_error_specific

// Rename print_error_to_stderr so we can mute it

#define print_error_to_stderr fuse_print_error_to_stderr
#include <fuse/ui.c>
#undef print_error_to_stderr

static int print_error_to_stderr(ui_error_level severity, const char *message)
{
   (void)severity;
   (void)message;
   return 0;
}

// Fix a bug in utils_find_file_path where stat is used instead of compat_file_exists

#define stat(a, b) (!compat_file_exists(a))
#include <fuse/utils.c>
#undef stat

// Change the call to utils_write_file in snapshot_write so we can trap the write and use libretro to save it for us

#define utils_write_file fuse_write_snapshot
#include <fuse/snapshot.c>
#undef utils_write_file
