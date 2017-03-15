/*
  weaver.c

  some functions herein are copied from the lua source code:
  ** $Id: lua.h,v 1.331 2016/05/30 15:53:28 roberto Exp roberto $
  ** Lua - A Scripting Language
  ** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
  ** See Copyright Notice at the end of this file

  */


#include <pthread.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "events.h"
#include "lua_eval.h"
#include "m.h"
#include "timers.h"
#include "oracle.h"
#include "weaver.h"

//------
//---- global lua state!
lua_State* lvm;

//-----------------------
//--- following functions are lifted from lua.c

//-----------------
//---- here resumes non-lifted code.

void w_run_code(const char* code) {
  l_dostring(lvm, code, "w_run_code");
  fflush(stdout);
}							

void w_handle_line(char* line) {
  l_handle_line(lvm, line);
}

//-------------
//--- declare lua->c glue

// grid
static int w_grid_set_led(lua_State* l);
static int w_grid_refresh(lua_State* l);
// audio engine
static int w_request_engine_report(lua_State* l);
static int w_load_engine(lua_State* l);
static int w_request_command_report(lua_State* l);
static int w_send_command(lua_State* l);
// timers
static int w_timer_start(lua_State* l);
static int w_timer_restart(lua_State* l);
static int w_timer_stop(lua_State* l);

// screen functions
// TODO
// static void w_screen_print(void);
// static extern void w_screen_draw();
//... ?

void w_init(void) {
  printf("starting lua vm \n");
  lvm = luaL_newstate();
  luaL_openlibs(lvm);
  lua_pcall(lvm, 0, 0, 0);
  fflush(stdout);

  // FIXME: how/where to document these in lua
  lua_register(lvm, "grid_set_led", &w_grid_set_led);
  lua_register(lvm, "grid_refresh", &w_grid_refresh);
  
  lua_register(lvm, "report_engines", &w_request_engine_report);
  lua_register(lvm, "load_engine", &w_load_engine);
  
  lua_register(lvm, "report_commands", &w_request_command_report);
  lua_register(lvm, "send_command", &w_send_command);

  lua_register(lvm, "timer_start", &w_timer_start);
  lua_register(lvm, "timer_stop", &w_timer_stop);
  
  // run system init code
  w_run_code("dofile(\"lua/norns.lua\");");
}

// run user startup code
// audio backend should be running
void w_user_startup(void) {
  lua_getglobal(lvm, "startup");
  l_report(lvm, l_docall(lvm, 0, 0));
}

//----------------------------------
//---- static definitions

int w_grid_set_led(lua_State* l) {
  struct m_dev* md;
  int x, y, z;
  if(lua_gettop(l) != 4) { // check num args
	goto args_error;
  }
  if(lua_islightuserdata(l, 1)) {
	md = lua_touserdata(l, 1);
  } else {
	goto args_error;
  }
  
  if(lua_isnumber(l, 2)) {
	x = lua_tonumber(l, 2) - 1; // convert from 1-base
  } else {
	goto args_error;
  }
	
  if(lua_isnumber(l, 3)) {
	y = lua_tonumber(l, 3) - 1; // convert from 1-base
  } else {
	goto args_error;
  }
  if(lua_isnumber(l, 4)) {
	z = lua_tonumber(l, 4); // don't convert value!
  } else {
	goto args_error;
  }
  
  m_dev_set_led(md, x, y, z);
  return 0;
  
 args_error:
  printf("warning: incorrect arguments to grid_set_led() \n"); fflush(stdout);
  return 1;
}

int w_grid_refresh(lua_State* l) {
  struct m_dev* md;
  if(lua_gettop(l) != 1) { // check num args
	goto args_error;
  }
  if(lua_islightuserdata(l, 1)) {
	md = lua_touserdata(l, 1);
  } else {
	goto args_error;
  }
  m_dev_refresh(md);
  return 0;
 args_error:
  printf("warning: incorrect arguments to grid_refresh() \n"); fflush(stdout);
  return 1;
}

//-- audio processing controls
int w_load_engine(lua_State* l) {
  if(lua_gettop(l) != 1) {
	goto args_error;
  }
  
  if(lua_isstring(l, 1)) {
	o_load_engine(lua_tostring(l, 1));
	return 0;
  } else {
	goto args_error;
  }
  
 args_error:
  printf("warning: incorrect arguments to load_engine() \n"); fflush(stdout);
  return 1;
}

int w_send_command(lua_State* l) {
  int nargs = lua_gettop(l);
  if(nargs < 1) { goto args_error; }

  char* cmd = NULL;
  char* fmt = NULL;

  if(lua_isnumber(l, 1)) {
	// FIXME? guess should be wrapped in descriptor access lock
	int idx = (int)lua_tonumber(l, 1) - 1; // 1-base to 0-base
	cmd = o_get_commands()[idx].cmd;
	fmt = o_get_commands()[idx].fmt;
  } else {
	printf("failed type check on first arg \n");
	goto args_error; }

  lo_message msg = lo_message_new();

  // debug
  const char* s;
  int d;
  double f;
  
  for(int i=2; i<=nargs; i++) {
	switch(fmt[i-2]) {
	case 's':
	  if(lua_isstring(l, i)) {
		s = lua_tostring(l, i);
		lo_message_add_string(msg, s );
	  } else {
		printf("failed string type check \n");
		goto args_error; }
	  break;
	case 'i':
	  if(lua_isnumber(l, i)) {
		d =  (int)lua_tonumber(l, i) ;
		lo_message_add_int32( msg, d);
	  } else { 
		printf("failed int type check \n");
		goto args_error; }
	  break;
	case 'f':
	  if(lua_isnumber(l, i)) {
		f = lua_tonumber(l, i);
		lo_message_add_double( msg, f );
	  } else { 
		printf("failed double type check \n");
		goto args_error; }
	  break;
	default:
	  break;
	}
  }

  if(cmd == NULL || fmt == NULL) {
	printf("error: null format/command string \n");
	return 1;
  } else {
	o_send_command(cmd, msg);
  }
  return 0;
  
 args_error:
  printf("warning: incorrect arguments to send_command() \n"); fflush(stdout);
  return 1;
}
  
int w_request_engine_report(lua_State* l) {
  o_request_engine_report();
}

int w_request_command_report(lua_State* l) {
  o_request_command_report();
}

// manage timers from lua
int w_timer_start(lua_State* l) {
  static int idx = 0;
  double seconds;
  int count, stage;
  int nargs = lua_gettop(l);
  if(nargs > 0) { // idx
	if(lua_isnumber(l, 1)) {
	  idx = lua_tonumber(l, 1) - 1; // convert from 1-based
	} else {
	  goto args_error;
	}
  }
  if(nargs > 1) { // seconds
	if(lua_isnumber(l, 2)) {
	  seconds = lua_tonumber(l, 2);
	} else {
	  goto args_error;
	}
  } else {
	seconds = 0.0; // timer will re-use previous value
  }
  if(nargs > 2) { // count
	if(lua_isnumber(l, 3)) {
	  count = lua_tonumber(l, 3);
	} else {
	  goto args_error;
	}
  } else {
	count = -1; // default: infinite
  }
  if(nargs > 3) { // stage
	if(lua_isnumber(l, 4)) { 
	  stage = lua_tonumber(l, 4) - 1; // convert from 1-based
	} else {
	  goto args_error;
	}
  } else {
	stage = 0;
  }
  timer_start(idx, seconds, count, stage);
  return 0;
 args_error:
  printf("warning: incorrect argument(s) to start_timer(); expected [nnnn] \n");
  fflush(stdout);
  return 1;
}

int w_timer_stop(lua_State* l) {
  int idx;
  if(lua_gettop(l) != 1) {
	goto args_error;
  }
  if(lua_isnumber(l, 1)) {
	idx = lua_tonumber(l, 1) - 1;
  } else {
	goto args_error;
  }
  timer_stop(idx);
  return 0;
 args_error:
  printf("warning: incorrect arguments to stop_timer() \n"); fflush(stdout);
  return 1;
}

//---- c -> lua glue

//--- hardware input

// helper for calling grid handlers
static inline void
w_call_grid_handler(int id, int x, int y, int state) {
  lua_getglobal(lvm, "monome");  
  lua_getfield(lvm, -1, "key"); 
  lua_remove(lvm, -2);
  lua_pushinteger(lvm, id+1); // convert to 1-base
  lua_pushinteger(lvm, x+1); // convert to 1-base
  lua_pushinteger(lvm, y+1); // convert to 1-base
  lua_pushinteger(lvm, state);
  l_report(lvm, l_docall(lvm, 4, 0));

}
void w_handle_grid_press(int id, int x, int y) {
  w_call_grid_handler( id, x, y, 1);
}

void w_handle_grid_lift(int id, int x, int y) {
  w_call_grid_handler( id, x, y, 0);
}

void w_handle_monome_add(void* mdev) {
  struct m_dev* md = (struct m_dev*)mdev;
  int id = m_dev_id(md);
  const char* serial =  m_dev_serial(md);
  const char* name =  m_dev_name(md);
  lua_getglobal(lvm, "monome");
  lua_getfield(lvm, -1, "add");
  lua_remove(lvm, -2);
  lua_pushinteger(lvm, id+1); // convert to 1-base
  lua_pushstring(lvm, serial);
  lua_pushstring(lvm, name);
  lua_pushlightuserdata(lvm, mdev);
  l_report(lvm, l_docall(lvm, 4, 0));
}

extern void w_handle_monome_remove(int id) {
  printf("w_handle_monome_remove()\n"); fflush(stdout);
  lua_getglobal(lvm, "monome");
  lua_getfield(lvm, -1, "remove");
  lua_remove(lvm, -2);
  lua_pushinteger(lvm, id+1); // convert to 1-base
  l_report(lvm, l_docall(lvm, 1, 0));
}

// helper for calling joystick handlers
static inline void
w_call_stick_handler(const char* name, int stick, int what, int val) {
  lua_getglobal(lvm, "joystick");  
  lua_getfield(lvm, -1, name); 
  lua_remove(lvm, -2); 
  lua_pushinteger(lvm, stick); 
  lua_pushinteger(lvm, what);
  lua_pushinteger(lvm, val);
  l_report(lvm, l_docall(lvm, 3, 0));
}

void w_handle_stick_axis(int stick, int axis, int value) {
  w_call_stick_handler("axis", stick+1, axis+1, value);
}

void w_handle_stick_button(int stick, int button, int value) {
  w_call_stick_handler("button", stick+1, button+1, value);
}

void w_handle_stick_hat(int stick, int hat, int value) {
  w_call_stick_handler("hat", stick+1, hat+1, value);
}
void w_handle_stick_ball(int stick, int ball, int xrel, int yrel) {
  lua_getglobal(lvm, "joystick");  
  lua_getfield(lvm, -1, "hat"); 
  lua_remove(lvm, -2); 
  lua_pushinteger(lvm, stick); 
  lua_pushinteger(lvm, ball);
  lua_pushinteger(lvm, xrel);
  lua_pushinteger(lvm, yrel);
  l_report(lvm, l_docall(lvm, 4, 0));
}

// helper for pushing array of c strings
static inline void
w_push_string_array(const char** arr, const int n) {
  // push a table of strings
  lua_createtable(lvm, n, 0);
  for (int i=0; i<n; i++) {
	lua_pushstring(lvm, arr[i]);
	lua_rawseti(lvm, -2, i+1);
  }
  // push count of entries
  lua_pushinteger(lvm, n);
}


// audio engine l_report handlers
void w_handle_engine_report(const char** arr, const int n) {
  lua_getglobal(lvm, "report");  
  lua_getfield(lvm, -1, "engines"); 
  lua_remove(lvm, -2);
  w_push_string_array(arr, n);
  l_report(lvm, l_docall(lvm, 2, 0));
}

void w_handle_command_report(const struct engine_command* arr,
							 const int num) {
  lua_getglobal(lvm, "report");
  lua_getfield(lvm, -1, "commands");
  lua_remove(lvm, -2);
  // push a table of tables: {{cmd, fmt}, {cmd,fmt}, ...}
  lua_createtable(lvm, num, 0);
  for(int i=0; i<num; i++) {
	lua_createtable(lvm, 2, 0);
	lua_pushstring(lvm, arr[i].cmd);
	lua_rawseti(lvm, -2, 1);
	lua_pushstring(lvm, arr[i].fmt);
	lua_rawseti(lvm, -2, 2);
	lua_rawseti(lvm, -2, i+1);
  }
  lua_pushinteger(lvm, num);
  l_report(lvm, l_docall(lvm, 2, 0));
}

// timer handler
void w_handle_timer(const int idx, const int stage) {
  lua_getglobal(lvm, "timer");
  lua_pushinteger(lvm, idx + 1);  // convert to 1-based
  lua_pushinteger(lvm, stage + 1); // convert to 1-based
  l_report(lvm, l_docall(lvm, 2, 0));
}