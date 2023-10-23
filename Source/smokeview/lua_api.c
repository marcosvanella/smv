#ifdef pp_LUA

#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include "options.h"

#include "smokeviewvars.h"

#include "infoheader.h"

#include "c_api.h"
#include "lua_api.h"

#include GLUT_H
#include "gd.h"

#if defined(_WIN32)
#include <direct.h>
#endif

lua_State *L;
int lua_displayCB(lua_State *L);

#ifdef WIN32
#define snprintf _snprintf
#else
#include <unistd.h>
#endif

char *ParseCommandline(int argc, char **argv);
void Usage(char *prog, int option);
int CheckSMVFile(char *file, char *subdir);

int ProgramSetupLua(lua_State *L, int argc, char **argv) {
  char *progname;
  InitVars();
  ParseCommonOptions(argc, argv);
  smv_filename = ParseCommandline(argc, argv);
  printf("smv_filename: %s\n", smv_filename);

  progname = argv[0];

  if (show_help == 1) {
    Usage("smokeview", HELP_SUMMARY);
    fflush(stderr);
    fflush(stdout);
    SMV_EXIT(0);
  }
  if (show_help == 2) {
    Usage("smokeview", HELP_ALL);
    fflush(stderr);
    fflush(stdout);
    SMV_EXIT(0);
  }
  prog_fullpath = progname;
#ifdef pp_LUA
  smokeview_bindir_abs = getprogdirabs(progname, &smokeviewpath);
#endif
  if (smokeview_bindir == NULL) {
    smokeview_bindir = GetProgDir(progname, &smokeviewpath);
  }
  if (show_version == 1 || smv_filename == NULL) {
    DisplayVersionInfo("Smokeview ");
    SMV_EXIT(0);
  }
  if (CheckSMVFile(smv_filename, smokeview_casedir) == 0) {
    SMV_EXIT(1);
  }
  InitTextureDir();
  InitScriptErrorFiles();
  smokezippath = GetSmokeZipPath(smokeview_bindir);
#ifdef WIN32
  have_ffmpeg = HaveProg("ffmpeg -version> Nul 2>Nul");
  have_ffplay = HaveProg("ffplay -version> Nul 2>Nul");
#else
  have_ffmpeg = HaveProg("ffmpeg -version >/dev/null 2>/dev/null");
  have_ffplay = HaveProg("ffplay -version >/dev/null 2>/dev/null");
#endif
  DisplayVersionInfo("Smokeview ");

  return 0;
}

/// @brief We can only take strings from the Lua interpreter as consts, as they
/// are 'owned' by the Lua code and we should not change them in C. This
/// function creates a copy that we can change in C.
char **copy_argv(const int argc, const char *const *argv_sv) {
  char **argv_sv_non_const;
  // Allocate pointers for list of smokeview arguments
  NewMemory((void **)&argv_sv_non_const, argc * sizeof(char *));
  // Allocate space for each smokeview argument
  int i;
  for (i = 0; i < argc; i++) {
    int length = strlen(argv_sv[i]);
    NewMemory((void **)&argv_sv_non_const[i], (length + 1) * sizeof(char));
    strcpy(argv_sv_non_const[i], argv_sv[i]);
  }
  return argv_sv_non_const;
}

/// @brief The corresponding function to free the memory allocated by copy_argv.
void free_argv(const int argc, char **argv_sv_non_const) {
  // Free the memory allocated for each argument
  int i;
  for (i = 0; i < argc; i++) {
    FREEMEMORY(argv_sv_non_const[i]);
  }
  // Free the memory allocated for the array
  FREEMEMORY(argv_sv_non_const);
}

int lua_SetupGLUT(lua_State *L) {
  int argc = lua_tonumber(L, 1);
  const char *const *argv_sv = lua_topointer(L, 2);
  // Here we must copy the arguments received from the Lua interperter to
  // allow them to be non-const (i.e. let the C code modify them).
  char **argv_sv_non_const = copy_argv(argc, argv_sv);
  SetupGlut(argc, argv_sv_non_const);
  free_argv(argc, argv_sv_non_const);
  return 0;
}

int lua_SetupCase(lua_State *L) {
  const char *filename = lua_tostring(L, -1);
  char *filename_mut;
  // Allocate some new memory in case smv tries to modify it.
  int f_len = strlen(filename);
  if (NewMemory((void **)&filename_mut, sizeof(char) * f_len + 1) == 0)
    return 2;
  strncpy(filename_mut, filename, f_len);
  filename_mut[f_len] = '\0';
  int return_code = SetupCase(filename_mut);
  lua_pushnumber(L, return_code);
  FREEMEMORY(filename_mut);
  return 1;
}

int RunLuaBranch(lua_State *L, int argc, char **argv) {
  int return_code;
  SetStdOut(stdout);
  initMALLOC();
  InitRandAB(1000000);
  // Setup the program, including parsing commandline arguments. Does not
  // initialise any graphical components.
  ProgramSetupLua(L, argc, argv);
  // From here on out, control is passed to the lua interpreter. All further
  // setup, including graphical display setup, is handled (or at least
  // triggered) by the interpreter.
  // TODO: currently the commands are issued here via C, but they are designed
  // such that they can be issued from lua.
  // Setup glut. TODO: this is currently done via to C because the commandline
  // arguments are required for glutInit.

  lua_pushnumber(L, argc);
  lua_pushlightuserdata(L, argv);
  lua_SetupGLUT(L);
  START_TIMER(startup_time);
  // Load information about smokeview into the lua interpreter.
  lua_initsmvproginfo(L);

  if (smv_filename == NULL) {
    return 0;
  }
  lua_pushstring(L, smv_filename);
  // TODO: only set up a case if one is specified, otherwise leave it to the
  // interpreter to call this.
  lua_SetupCase(L);
  return_code = lua_tonumber(L, -1);

  if (return_code == 0 && update_bounds == 1) {
    INIT_PRINT_TIMER(timer_update_bounds);
    return_code = Update_Bounds();
    PRINT_TIMER(timer_update_bounds, "Update_Bounds");
  }
  if (return_code != 0) return 1;
  if (convert_ini == 1) {
    INIT_PRINT_TIMER(timer_read_ini);
    ReadIni(ini_from);
    PRINT_TIMER(timer_read_ini, "ReadIni");
  }
  if (runhtmlscript == 1) {
    DoScriptHtml();
  }
  STOP_TIMER(startup_time);
  if (runhtmlscript == 1) {
    return 0;
  }

  glutMainLoop();
  return 0;
}

/// @brief Run a script.
/// @details There are two options for scripting, Lua and SSF. Which is run is
/// set here based on the commandline arguments. If either (exclusive) of these
/// values are set to true, then that script will run from within the display
/// callback (DisplayCB, in callbacks.c). These two loading routines are
/// included to load the scripts early in the piece, before the display
/// callback. Both runluascript and runscript are global.
int load_script(const char *filename) {
  if (runluascript == 1 && runscript == 1) {
    fprintf(stderr, "Both a Lua script and an SSF script cannot be run "
                    "simultaneously\n");
    exit(1);
  }
  if (runluascript == 1) {
    // Load the Lua script in order for it to be run later.
    if (loadLuaScript(filename) != LUA_OK) {
      fprintf(stderr, "There was an error loading the script, and so it "
                      "will not run.\n");
      if (exit_on_script_crash) {
        exit(1); // exit with an error code
      }
      runluascript = 0; // set this to false so that the smokeview no longer
                        // tries to run the script as it failed to load
      fprintf(stderr, "Running smokeview normally.\n");
    } else {
      fprintf(stderr, "%s successfully loaded\n", filename);
    }
  }
#ifdef pp_LUA_SSF
  if (runscript == 1) {
    // Load the ssf script in order for it to be run later
    // This still uses the Lua interpreter
    if (loadSSFScript(filename) != LUA_OK) {
      fprintf(stderr, "There was an error loading the script, and so it "
                      "will not run.\n");
      if (exit_on_script_crash) {
        exit(1); // exit with an error code
      }
      runluascript = 0; // set this to false so that the smokeview no longer
                        // tries to run the script as it failed to load
      fprintf(stderr, "Running smokeview normally.\n");
    }
  }
#endif
  return 1;
}

/// @brief Load a .smv file. This is currently not used as it is dependent on
/// Smokeview being able to run without a .smv file loaded.
int lua_loadsmvall(lua_State *L) {
  // The first argument is taken from the stack as a string.
  const char *filepath = lua_tostring(L, 1);
  // The function from the C api is called using this string.
  loadsmvall(filepath);
  // 0 arguments are returned.
  return 0;
}

/// @brief Set render clipping.
int lua_renderclip(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  int left = lua_tonumber(L, 2);
  int right = lua_tonumber(L, 3);
  int bottom = lua_tonumber(L, 4);
  int top = lua_tonumber(L, 5);
  renderclip(flag, left, right, bottom, top);
  return 0;
}

/// @brief Render the current frame to a file.
int lua_render(lua_State *L) {
  lua_displayCB(L);
  const char *basename = lua_tostring(L, 1);
  int ret = render(basename);
  lua_pushnumber(L, ret);
  return 1;
}

/// @brief Returns an error code then the image data.
int lua_render_var(lua_State *L) {
  gdImagePtr RENDERimage;
  int return_code;
  char *imageData;
  int imageSize;

  // render image to RENDERimage gd buffer
  return_code = RenderFrameLuaVar(VIEW_CENTER, &RENDERimage);
  lua_pushnumber(L, return_code);
  // convert to a simpler byte-buffer
  imageData = gdImagePngPtr(RENDERimage, &imageSize);
  // push to stack
  lua_pushlstring(L, imageData, imageSize);
  // destroy C copy
  gdImageDestroy(RENDERimage);

  return 2;
}

int lua_gsliceview(lua_State *L) {
  int data = lua_tonumber(L, 1);
  int show_triangles = lua_toboolean(L, 2);
  int show_triangulation = lua_toboolean(L, 3);
  int show_normal = lua_toboolean(L, 4);
  gsliceview(data, show_triangles, show_triangulation, show_normal);
  return 0;
}

int lua_showplot3ddata(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  int plane_orientation = lua_tonumber(L, 2);
  int display = lua_tonumber(L, 3);
  int showhide = lua_toboolean(L, 4);
  float position = lua_tonumber(L, 5);
  ShowPlot3dData(meshnumber, plane_orientation, display, showhide, position, 0);
  return 0;
}

int lua_gslicepos(lua_State *L) {
  float x = lua_tonumber(L, 1);
  float y = lua_tonumber(L, 2);
  float z = lua_tonumber(L, 3);
  gslicepos(x, y, z);
  return 0;
}
int lua_gsliceorien(lua_State *L) {
  float az = lua_tonumber(L, 1);
  float elev = lua_tonumber(L, 2);
  gsliceorien(az, elev);
  return 0;
}

int lua_settourview(lua_State *L) {
  int edittourArg = lua_tonumber(L, 1);
  int mode = lua_tonumber(L, 2);
  int show_tourlocusArg = lua_toboolean(L, 3);
  float tour_global_tensionArg = lua_tonumber(L, 4);
  settourview(edittourArg, mode, show_tourlocusArg, tour_global_tensionArg);
  return 0;
}

int lua_settourkeyframe(lua_State *L) {
  float keyframe_time = lua_tonumber(L, 1);
  settourkeyframe(keyframe_time);
  return 0;
}

/// @brief Trigger the display callback.
int lua_displayCB(lua_State *L) {
  // runluascript=0;
  DisplayCB();
  // runluascript=1;
  return 0;
}

/// @brief Hide the smokeview window. This should not currently be used as it
/// prevents the display callback being called, and therefore the script will
/// not continue (the script is called as part of the display callback).
int lua_hidewindow(lua_State *L) {
  glutHideWindow();
  // once we hide the window the display callback is never called
  return 0;
}

/// @brief By calling yieldscript, the script is suspended and the smokeview
/// display is updated. It is necessary to call this before producing any
/// outputs (such as renderings).
int lua_yieldscript(lua_State *L) {
  lua_yield(L, 0 /*zero results*/);
  return 0;
}

/// @brief As with lua_yieldscript, but immediately resumes the script after
/// letting the display callback run.
int lua_tempyieldscript(lua_State *L) {
  runluascript = 1;
  lua_yield(L, 0 /*zero results*/);
  return 0;
}

/// @brief Return the current frame number which Smokeivew has loaded.
int lua_getframe(lua_State *L) {
  int framenumber = getframe();
  // Push a return value to the Lua stack.
  lua_pushinteger(L, framenumber);
  // Tell Lua that there is a single return value left on the stack.
  return 1;
}

/// @brief Shift to a specific frame number.
int lua_setframe(lua_State *L) {
  int f = lua_tonumber(L, 1);
  setframe(f);
  return 0;
}

/// @brief Get the time value of the currently loaded frame.
int lua_gettime(lua_State *L) {
  if (global_times != NULL && nglobal_times > 0) {
    float time = gettime();
    lua_pushnumber(L, time);
    return 1;
  } else {
    return 0;
  }
}

/// @brief Shift to the closest frame to given a time value.
int lua_settime(lua_State *L) {
  lua_displayCB(L);
  float t = lua_tonumber(L, 1);
  int return_code = settime(t);
  lua_pushnumber(L, return_code);
  return 1;
}

/// @brief Load an FDS data file directly (i.e. as a filepath).
int lua_loaddatafile(lua_State *L) {
  const char *filename = lua_tostring(L, 1);
  int return_value = loadfile(filename);
  lua_pushnumber(L, return_value);
  return 1;
}

/// @brief Load a Smokeview config (.ini) file.
int lua_loadinifile(lua_State *L) {
  const char *filename = lua_tostring(L, 1);
  loadinifile(filename);
  return 0;
}

/// @brief Load an FDS vector data file directly (i.e. as a filepath). This
/// function handles the loading of any additional data files necessary to
/// display vectors.
int lua_loadvdatafile(lua_State *L) {
  const char *filename = lua_tostring(L, 1);
  int return_value = loadvfile(filename);
  lua_pushnumber(L, return_value);
  return 1;
}

/// @brief Load an FDS boundary file directly (i.e. as a filepath). This is
/// equivalent to lua_loadfile, but specialised for boundary files. This is
/// included to reflect the underlying code.
int lua_loadboundaryfile(lua_State *L) {
  const char *filename = lua_tostring(L, 1);
  loadboundaryfile(filename);
  return 0;
}

/// @brief Load a slice file given the type of slice, the axis along which it
/// exists and its position along this axis.
int lua_loadslice(lua_State *L) {
  const char *type = lua_tostring(L, 1);
  int axis = lua_tonumber(L, 2);
  float distance = lua_tonumber(L, 3);
  loadslice(type, axis, distance);
  return 0;
}

/// @brief Load a slice based on its index in sliceinfo.
int lua_loadsliceindex(lua_State *L) {
  size_t index = lua_tonumber(L, 1);
  int error = 0;
  loadsliceindex(index, &error);
  if (error) {
    return luaL_error(L, "Could not load slice at index %zu", index);
  }
  return 0;
}

int lua_get_clipping_mode(lua_State *L) {
  lua_pushnumber(L, get_clipping_mode());
  return 1;
}

/// @brief Set the clipping mode, which determines which parts of the model are
/// clipped (based on the set clipping values). This function takes an int,
/// which is one
///  of:
///    0: No clipping.
///    1: Clip blockages and data.
///    2: Clip blockages.
///    3: Clip data.
int lua_set_clipping_mode(lua_State *L) {
  int mode = lua_tonumber(L, 1);
  set_clipping_mode(mode);
  return 0;
}

int lua_set_sceneclip_x(lua_State *L) {
  int clipMin = lua_toboolean(L, 1);
  float min = lua_tonumber(L, 2);
  int clipMax = lua_toboolean(L, 3);
  float max = lua_tonumber(L, 4);
  set_sceneclip_x(clipMin, min, clipMax, max);
  return 0;
}

int lua_set_sceneclip_x_min(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  float value = lua_tonumber(L, 2);
  set_sceneclip_x_min(flag, value);
  return 0;
}

int lua_set_sceneclip_x_max(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  float value = lua_tonumber(L, 2);
  set_sceneclip_x_max(flag, value);
  return 0;
}

int lua_set_sceneclip_y(lua_State *L) {
  int clipMin = lua_toboolean(L, 1);
  float min = lua_tonumber(L, 2);
  int clipMax = lua_toboolean(L, 3);
  float max = lua_tonumber(L, 4);
  set_sceneclip_y(clipMin, min, clipMax, max);
  return 0;
}

int lua_set_sceneclip_y_min(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  float value = lua_tonumber(L, 2);
  set_sceneclip_y_min(flag, value);
  return 0;
}

int lua_set_sceneclip_y_max(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  float value = lua_tonumber(L, 2);
  set_sceneclip_y_max(flag, value);
  return 0;
}

int lua_set_sceneclip_z(lua_State *L) {
  int clipMin = lua_toboolean(L, 1);
  float min = lua_tonumber(L, 2);
  int clipMax = lua_toboolean(L, 3);
  float max = lua_tonumber(L, 4);
  set_sceneclip_z(clipMin, min, clipMax, max);
  return 0;
}

int lua_set_sceneclip_z_min(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  float value = lua_tonumber(L, 2);
  set_sceneclip_z_min(flag, value);
  return 0;
}

int lua_set_sceneclip_z_max(lua_State *L) {
  int flag = lua_toboolean(L, 1);
  float value = lua_tonumber(L, 2);
  set_sceneclip_z_max(flag, value);
  return 0;
}

/// @brief Return a table (an array) of the times available in Smokeview. They
/// key of the table is an int representing the frame number, and the value of
/// the table is a float representing the time.
/// @param L The lua interpreter
/// @return Number of stack items left on stack.
int lua_get_global_times(lua_State *L) {
  lua_createtable(L, 0, nglobal_times);
  int i;
  for (i = 0; i < nglobal_times; i++) {
    lua_pushnumber(L, i);
    lua_pushnumber(L, global_times[i]);
    lua_settable(L, -3);
  }
  return 1;
}

/// @brief Given a frame number return the time.
/// @param L The lua interpreter
/// @return Number of stack items left on stack.
int lua_get_global_time(lua_State *L) {
  int frame_number = lua_tonumber(L, 1);
  if (frame_number >= 0 && frame_number < nglobal_times) {
    lua_pushnumber(L, global_times[frame_number]);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

/// @brief Get the number of (global) frames available to smokeview.
/// @param L
/// @return
int lua_get_nglobal_times(lua_State *L) {
  lua_pushnumber(L, nglobal_times);
  return 1;
}

/// @brief Get the number of meshes in the loaded model.
int lua_get_nmeshes(lua_State *L) {
  lua_pushnumber(L, nmeshes);
  return 1;
}

/// @brief Get the number of particle files in the loaded model.
int lua_get_npartinfo(lua_State *L) {
  lua_pushnumber(L, npartinfo);
  return 1;
}

int lua_getiblankcell(lua_State *L) {
  // The offset in the global meshinfo table.
  int mesh_index = lua_tonumber(L, lua_upvalueindex(1));
  // The offsets into the mesh requested
  int i = lua_tonumber(L, 1);
  int j = lua_tonumber(L, 2);
  int k = lua_tonumber(L, 3);

  meshdata *mesh = &meshinfo[mesh_index];
  char iblank =
      mesh->c_iblank_cell[(i) + (j)*mesh->ibar + (k)*mesh->ibar * mesh->jbar];
  if (iblank == GAS) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

int lua_getiblanknode(lua_State *L) {
  // The offset in the global meshinfo table.
  int mesh_index = lua_tonumber(L, lua_upvalueindex(1));
  // The offsets into the mesh requested.
  int i = lua_tonumber(L, 1);
  int j = lua_tonumber(L, 2);
  int k = lua_tonumber(L, 3);

  meshdata *mesh = &meshinfo[mesh_index];
  char iblank = mesh->c_iblank_node[(i) + (j) * (mesh->ibar + 1) +
                                    (k) * (mesh->ibar + 1) * (mesh->jbar + 1)];
  if (iblank == GAS) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

/// @brief Build a Lua table with information on the meshes of the model. The
/// key of the table is the mesh number.
// TODO: provide more information via this interface.
int lua_get_meshes(lua_State *L) {
  int entries = nmeshes;
  meshdata *infotable = meshinfo;
  lua_createtable(L, 0, entries);
  int i;
  for (i = 0; i < entries; i++) {
    lua_pushnumber(L, i);
    lua_createtable(L, 0, 5);

    lua_pushnumber(L, infotable[i].ibar);
    lua_setfield(L, -2, "ibar");

    lua_pushnumber(L, infotable[i].jbar);
    lua_setfield(L, -2, "jbar");

    lua_pushnumber(L, infotable[i].kbar);
    lua_setfield(L, -2, "kbar");

    lua_pushstring(L, infotable[i].label);
    lua_setfield(L, -2, "label");

    lua_pushnumber(L, infotable[i].kbar);
    lua_setfield(L, -2, "cellsize");

    lua_pushnumber(L, xbar0);
    lua_setfield(L, -2, "xbar0");

    lua_pushnumber(L, ybar0);
    lua_setfield(L, -2, "ybar0");

    lua_pushnumber(L, zbar0);
    lua_setfield(L, -2, "zbar0");

    lua_pushnumber(L, xyzmaxdiff);
    lua_setfield(L, -2, "xyzmaxdiff");

    lua_pushnumber(L, i);
    lua_pushcclosure(L, lua_getiblankcell, 1);
    lua_setfield(L, -2, "iblank_cell");

    lua_pushnumber(L, i);
    lua_pushcclosure(L, lua_getiblanknode, 1);
    lua_setfield(L, -2, "iblank_node");

    // loop for less than ibar
    int j;
    lua_createtable(L, 0, infotable[i].ibar);
    for (j = 0; j < infotable[i].ibar; j++) {
      lua_pushnumber(L, j);
      lua_pushnumber(L, infotable[i].xplt[j]);
      lua_settable(L, -3);
    }
    lua_setfield(L, -2, "xplt");

    lua_createtable(L, 0, infotable[i].jbar);
    for (j = 0; j < infotable[i].jbar; j++) {
      lua_pushnumber(L, j);
      lua_pushnumber(L, infotable[i].yplt[j]);
      lua_settable(L, -3);
    }
    lua_setfield(L, -2, "yplt");

    lua_createtable(L, 0, infotable[i].kbar);
    for (j = 0; j < infotable[i].kbar; j++) {
      lua_pushnumber(L, j);
      lua_pushnumber(L, infotable[i].zplt[j]);
      lua_settable(L, -3);
    }
    lua_setfield(L, -2, "zplt");

    lua_createtable(L, 0, infotable[i].ibar);
    for (j = 0; j < infotable[i].ibar; j++) {
      lua_pushnumber(L, j);
      lua_pushnumber(L, infotable[i].xplt_orig[j]);
      lua_settable(L, -3);
    }
    lua_setfield(L, -2, "xplt_orig");

    lua_createtable(L, 0, infotable[i].jbar);
    for (j = 0; j < infotable[i].jbar; j++) {
      lua_pushnumber(L, j);
      lua_pushnumber(L, infotable[i].yplt_orig[j]);
      lua_settable(L, -3);
    }
    lua_setfield(L, -2, "yplt_orig");

    lua_createtable(L, 0, infotable[i].kbar);
    for (j = 0; j < infotable[i].kbar; j++) {
      lua_pushnumber(L, j);
      lua_pushnumber(L, infotable[i].zplt_orig[j]);
      lua_settable(L, -3);
    }
    lua_setfield(L, -2, "zplt_orig");

    lua_settable(L, -3);
  }
  // Leaves one returned value on the stack, the mesh table.
  return 1;
}

/// @brief Get the number of meshes in the loaded model.
int lua_get_ndevices(lua_State *L) {
  lua_pushnumber(L, ndeviceinfo);
  return 1;
}

/// @brief Build a Lua table with information on the devices of the model.
int lua_get_devices(lua_State *L) {
  int entries = ndeviceinfo;
  devicedata *infotable = deviceinfo;
  lua_createtable(L, 0, entries);
  int i;
  for (i = 0; i < entries; i++) {
    lua_pushstring(L, infotable[i].deviceID);
    lua_createtable(L, 0, 2);

    lua_pushstring(L, infotable[i].deviceID);
    lua_setfield(L, -2, "label");

    lua_settable(L, -3);
  }
  return 1;
}

int lua_create_vector(lua_State *L, csvdata *csv_x, csvdata *csv_y) {
  size_t i;
  lua_createtable(L, 0, 3);

  lua_pushstring(L, csv_y->label.longlabel);
  lua_setfield(L, -2, "name");

  // x-vector
  lua_createtable(L, 0, 3);
  lua_pushstring(L, csv_x->label.longlabel);
  lua_setfield(L, -2, "name");
  lua_pushstring(L, csv_x->label.unit);
  lua_setfield(L, -2, "units");
  lua_createtable(L, 0, csv_x->nvals);
  for (i = 0; i < csv_x->nvals; ++i) {
    lua_pushnumber(L, i + 1);
    lua_pushnumber(L, csv_x->vals[i]);
    lua_settable(L, -3);
  }
  lua_setfield(L, -2, "values");
  lua_setfield(L, -2, "x");
  // y-vector
  lua_createtable(L, 0, 3);
  lua_pushstring(L, csv_y->label.longlabel);
  lua_setfield(L, -2, "name");
  lua_pushstring(L, csv_y->label.unit);
  lua_setfield(L, -2, "units");
  lua_createtable(L, 0, csv_y->nvals);
  for (i = 0; i < csv_y->nvals; ++i) {
    lua_pushnumber(L, i + 1);
    lua_pushnumber(L, csv_y->vals[i]);
    lua_settable(L, -3);
  }
  lua_setfield(L, -2, "values");
  lua_setfield(L, -2, "y");
  return 1;
}

/// @brief Get the number of CSV files available to the model.
int lua_get_ncsvinfo(lua_State *L) {
  lua_pushnumber(L, ncsvfileinfo);
  return 1;
}

csvfiledata *get_csvinfo(const char *key) {
  // Loop through csvinfo until we find the right entry
  size_t i;
  for (i = 0; i < ncsvfileinfo; ++i) {
    if (strcmp(csvfileinfo[i].c_type, key) == 0) {
      return &csvfileinfo[i];
    }
  }
  return NULL;
}

int get_csvindex(const char *key) {
  // Loop through csvinfo until we find the right entry
  size_t i;
  for (i = 0; i < ncsvfileinfo; ++i) {
    if (strcmp(csvfileinfo[i].c_type, key) == 0) {
      return i;
    }
  }
  return -1;
}

void load_csv(csvfiledata *csventry) {
  ReadCSVFile(csventry, LOAD);
  csventry->loaded = 1;
}

int lua_load_csv(lua_State *L) {
  lua_pushstring(L, "c_type");
  lua_gettable(L, 1);
  const char *key = lua_tostring(L, -1);
  csvfiledata *csventry = get_csvinfo(key);
  if (csventry == NULL) return 0;
  load_csv(csventry);
  return 0;
}

int access_csventry_prop(lua_State *L) {
  // Take the index from the table.
  lua_pushstring(L, "index");
  lua_gettable(L, 1);
  int index = lua_tonumber(L, -1);
  const char *field = lua_tostring(L, 2);
  if (strcmp(field, "loaded") == 0) {
    lua_pushboolean(L, csvfileinfo[index].loaded);
    return 1;
  } else if (strcmp(field, "display") == 0) {
    lua_pushboolean(L, csvfileinfo[index].display);
    return 1;
  } else if (strcmp(field, "vectors") == 0) {
    csvfiledata *csventry = &csvfileinfo[index];
    if (!csventry->loaded) {
      load_csv(csventry);
    }
    // TODO: don't create every time
    lua_createtable(L, 0, csventry->ncsvinfo);
    size_t j;
    for (j = 0; j < csventry->ncsvinfo; j++) {
      // Load vector data into lua.
      // TODO: change to access indirectly rater than copying via stack
      // printf("adding: %s\n", csventry->vectors[j].y->name);

      lua_create_vector(L, csventry->time, &(csventry->csvinfo[j]));
      lua_setfield(L, -2, csventry->csvinfo[j].label.longlabel);
    }
    return 1;
  } else {
    return 0;
  }
}

int lua_csv_is_loaded(lua_State *L) {
  const char *key = lua_tostring(L, lua_upvalueindex(1));
  csvfiledata *csventry = get_csvinfo(key);
  lua_pushboolean(L, csventry->loaded);
  return 1;
}

/// @brief Create a table so that a metatable can be used.
int lua_get_csvdata(lua_State *L) {
  // L1 is the table
  // L2 is the string key
  const char *key = lua_tostring(L, 2);
  // char *file = lua_tostring(L, 1);
  csvfiledata *csventry = get_csvinfo(key);
  // Check if the chosen csv data is loaded
  if (!csventry->loaded) {
    // Load the data.
    load_csv(csventry);
  }
  // TODO: put userdata on stack
  lua_pushlightuserdata(L, csventry->csvinfo);
  return 1;
}

int access_pl3dentry_prop(lua_State *L) {
  // Take the index from the table.
  lua_pushstring(L, "index");
  lua_gettable(L, 1);
  int index = lua_tonumber(L, -1);
  const char *field = lua_tostring(L, 2);
  if (strcmp(field, "loaded") == 0) {
    lua_pushboolean(L, plot3dinfo[index].loaded);
    return 1;
  } else {
    return 0;
  }
}

int lua_set_pl3d_bound_min(lua_State *L) {
  int pl3dValueIndex = lua_tonumber(L, 1);
  int set = lua_toboolean(L, 2);
  float value = lua_tonumber(L, 3);
  set_pl3d_bound_min(pl3dValueIndex, set, value);
  return 0;
}

int lua_set_pl3d_bound_max(lua_State *L) {
  int pl3dValueIndex = lua_tonumber(L, 1);
  int set = lua_toboolean(L, 2);
  float value = lua_tonumber(L, 3);
  set_pl3d_bound_max(pl3dValueIndex, set, value);
  return 0;
}

/// @brief Get the number of PL3D files available to the model.
int lua_get_nplot3dinfo(lua_State *L) {
  lua_pushnumber(L, nplot3dinfo);
  return 1;
}

int lua_get_plot3dentry(lua_State *L) {
  int lua_index = lua_tonumber(L, -1);
  int index = lua_index - 1;
  int i;

  // csvdata *csventry = get_csvinfo(key);
  // fprintf(stderr, "csventry->file: %s\n", csventry->file);
  lua_createtable(L, 0, 4);

  lua_pushnumber(L, index);
  lua_setfield(L, -2, "index");

  lua_pushstring(L, plot3dinfo[index].file);
  lua_setfield(L, -2, "file");

  lua_pushstring(L, plot3dinfo[index].reg_file);
  lua_setfield(L, -2, "reg_file");

  lua_pushstring(L, plot3dinfo[index].longlabel);
  lua_setfield(L, -2, "longlabel");

  lua_pushnumber(L, plot3dinfo[index].time);
  lua_setfield(L, -2, "time");

  lua_pushnumber(L, plot3dinfo[index].u);
  lua_setfield(L, -2, "u");
  lua_pushnumber(L, plot3dinfo[index].v);
  lua_setfield(L, -2, "v");
  lua_pushnumber(L, plot3dinfo[index].w);
  lua_setfield(L, -2, "w");

  lua_pushnumber(L, plot3dinfo[index].nvars);
  lua_setfield(L, -2, "nvars");

  lua_pushnumber(L, plot3dinfo[index].blocknumber);
  lua_setfield(L, -2, "blocknumber");

  lua_pushnumber(L, plot3dinfo[index].display);
  lua_setfield(L, -2, "display");

  lua_createtable(L, 0, 6);
  for (i = 0; i < 6; ++i) {
    lua_pushnumber(L, i + 1);

    lua_createtable(L, 0, 3);
    lua_pushstring(L, plot3dinfo[index].label[i].longlabel);
    lua_setfield(L, -2, "longlabel");
    lua_pushstring(L, plot3dinfo[index].label[i].shortlabel);
    lua_setfield(L, -2, "shortlabel");
    lua_pushstring(L, plot3dinfo[index].label[i].unit);
    lua_setfield(L, -2, "unit");

    lua_settable(L, -3);
  }
  lua_setfield(L, -2, "label");

  // Create a metatable.
  // TODO: this metatable might be more easily implemented directly in Lua
  // so that we don't need to reimplement table access.
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &access_pl3dentry_prop);
  lua_setfield(L, -2, "__index");
  // then set the metatable
  lua_setmetatable(L, -2);

  return 1;
}

int lua_get_plot3dinfo(lua_State *L) {
  lua_createtable(L, 0, nplot3dinfo);
  int i;
  for (i = 0; i < nplot3dinfo; i++) {
    lua_pushnumber(L, i + 1);
    lua_get_plot3dentry(L);

    lua_settable(L, -3);
  }
  return 1;
}

int lua_get_qdata_sum(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  int vari, i, j, k;
  meshdata mesh = meshinfo[meshnumber];
  int ntotal = (mesh.ibar + 1) * (mesh.jbar + 1) * (mesh.kbar + 1);
  int vars = 5;
  float totals[5];
  totals[0] = 0.0;
  totals[1] = 0.0;
  totals[2] = 0.0;
  totals[3] = 0.0;
  totals[4] = 0.0;
  for (vari = 0; vari < 5; ++vari) {
    int offset = vari * ntotal;
    for (k = 0; k <= mesh.kbar; ++k) {
      for (j = 0; j <= mesh.jbar; ++j) {
        for (i = 0; i <= mesh.ibar; ++i) {
          int n = offset + k * (mesh.jbar + 1) * (mesh.ibar + 1) +
                  j * (mesh.ibar + 1) + i;
          totals[vari] += mesh.qdata[n];
        }
      }
    }
  }
  for (vari = 0; vari < vars; ++vari) {
    lua_pushnumber(L, totals[vari]);
  }
  lua_pushnumber(L, ntotal);
  return vars + 1;
}

/// @brief Sum bounded data in a given mesh
int lua_get_qdata_sum_bounded(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  int vari, i, j, k;
  int i1, i2, j1, j2, k1, k2;
  i1 = lua_tonumber(L, 2);
  i2 = lua_tonumber(L, 3);
  j1 = lua_tonumber(L, 4);
  j2 = lua_tonumber(L, 5);
  k1 = lua_tonumber(L, 6);
  k2 = lua_tonumber(L, 7);
  meshdata mesh = meshinfo[meshnumber];
  int ntotal = (mesh.ibar + 1) * (mesh.jbar + 1) * (mesh.kbar + 1);
  int bounded_total = (i2 - i1 + 1) * (j2 - j1 + 1) * (k2 - k1 + 1);
  int vars = 5;
  float totals[5];
  totals[0] = 0.0;
  totals[1] = 0.0;
  totals[2] = 0.0;
  totals[3] = 0.0;
  totals[4] = 0.0;
  for (vari = 0; vari < 5; ++vari) {
    int offset = vari * ntotal;
    for (k = k1; k <= k2; ++k) {
      for (j = j1; j <= j2; ++j) {
        for (i = i1; i <= i2; ++i) {
          int n = offset + k * (mesh.jbar + 1) * (mesh.ibar + 1) +
                  j * (mesh.ibar + 1) + i;
          totals[vari] += mesh.qdata[n];
        }
      }
    }
  }

  for (vari = 0; vari < vars; ++vari) {
    lua_pushnumber(L, totals[vari]);
  }
  lua_pushnumber(L, bounded_total);
  return vars + 1;
}

/// @brief Sum bounded data in a given mesh
int lua_get_qdata_max_bounded(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  int vari, i, j, k;
  int i1, i2, j1, j2, k1, k2;
  i1 = lua_tonumber(L, 2);
  i2 = lua_tonumber(L, 3);
  j1 = lua_tonumber(L, 4);
  j2 = lua_tonumber(L, 5);
  k1 = lua_tonumber(L, 6);
  k2 = lua_tonumber(L, 7);
  meshdata mesh = meshinfo[meshnumber];
  int ntotal = (mesh.ibar + 1) * (mesh.jbar + 1) * (mesh.kbar + 1);
  int bounded_total = (i2 - i1 + 1) * (j2 - j1 + 1) * (k2 - k1 + 1);
  int vars = 5;
  float maxs[5];
  maxs[0] = -1 * FLT_MAX;
  maxs[1] = -1 * FLT_MAX;
  maxs[2] = -1 * FLT_MAX;
  maxs[3] = -1 * FLT_MAX;
  maxs[4] = -1 * FLT_MAX;
  for (vari = 0; vari < 5; ++vari) {
    int offset = vari * ntotal;
    for (k = k1; k <= k2; ++k) {
      for (j = j1; j <= j2; ++j) {
        for (i = i1; i <= i2; ++i) {
          int n = offset + k * (mesh.jbar + 1) * (mesh.ibar + 1) +
                  j * (mesh.ibar + 1) + i;
          if (maxs[vari] < mesh.qdata[n]) {
            maxs[vari] = mesh.qdata[n];
          }
        }
      }
    }
  }

  for (vari = 0; vari < vars; ++vari) {
    lua_pushnumber(L, maxs[vari]);
  }
  lua_pushnumber(L, bounded_total);
  return vars + 1;
}

int lua_get_qdata_mean(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  int vari, i, j, k;
  meshdata mesh = meshinfo[meshnumber];
  int ntotal = (mesh.ibar + 1) * (mesh.jbar + 1) * (mesh.kbar + 1);
  int vars = 5;
  float totals[5];
  totals[0] = 0.0;
  totals[1] = 0.0;
  totals[2] = 0.0;
  totals[3] = 0.0;
  totals[4] = 0.0;
  for (vari = 0; vari < 5; ++vari) {
    int offset = vari * ntotal;
    for (k = 0; k <= mesh.kbar; ++k) {
      for (j = 0; j <= mesh.jbar; ++j) {
        for (i = 0; i <= mesh.ibar; ++i) {
          int n = offset + k * (mesh.jbar + 1) * (mesh.ibar + 1) +
                  j * (mesh.ibar + 1) + i;
          totals[vari] += mesh.qdata[n];
        }
      }
    }
  }
  for (vari = 0; vari < vars; ++vari) {
    lua_pushnumber(L, totals[vari] / ntotal);
  }
  return vars;
}

int lua_get_global_time_n(lua_State *L) {
  // argument 1 is the table, argument 2 is the index
  int index = lua_tonumber(L, 2);
  if (index < 0 || index >= nglobal_times) {
    return luaL_error(L, "%d is not a valid global time index\n", index);
  }
  lua_pushnumber(L, global_times[index]);
  return 1;
}

// TODO: remove this from a hardcoded string.
int setup_pl3dtables(lua_State *L) {
  luaL_dostring(L, "\
    pl3d = {}\
    local allpl3dtimes = {}\
    for i,v in ipairs(plot3dinfo) do\
        if not allpl3dtimes[v.time] then allpl3dtimes[v.time] = {} end\
        assert(not allpl3dtimes[v.time][v.blocknumber+1])\
        allpl3dtimes[v.time][v.blocknumber+1] = v\
    end\
    local pl3dtimes = {}\
    for k,v in pairs(allpl3dtimes) do\
        pl3dtimes[#pl3dtimes+1] = {time = k, entries = v}\
    end\
    table.sort( pl3dtimes, function(a,b) return a.time < b.time end)\
    pl3d.entries = plot3dinfo\
    pl3d.frames = pl3dtimes");
  return 0;
}

int lua_case_title(lua_State *L) {
  lua_pushstring(L, "chid");
  lua_gettable(L, 1);
  const char *chid = lua_tostring(L, -1);
  const char *name = lua_tostring(L, 2);
  lua_pushfstring(L, "%s for %s", name, chid);
  return 1;
}

int lua_case_index(lua_State *L) {
  const char *field = lua_tostring(L, 2);
  if (strcmp(field, "chid") == 0) {
    lua_pushstring(L, chidfilebase);
    return 1;
  } else {
    return 0;
  }
}

int lua_case_newindex(lua_State *L) {
  const char *field = lua_tostring(L, 2);
  if (strcmp(field, "chid") == 0) {
    luaL_error(L, "case.chid is read-only");
    // lua_pushstring(L, value);
    // lua_setrenderdir(L);
    return 0;
  } else {
    return 0;
  }
}

/// @brief Load data about the loaded module into the lua interpreter. This
/// initsmvdata is necessary to bring some data into the Lua interpreter from
/// the model. This is included here rather than doing in the Smokeview code to
/// increase separation. This will likely be removed in future versions.
// TODO: Consider converting most of these to userdata, rather than copying them
// into the lua interpreter.
int lua_create_case(lua_State *L) {
  // Create case table
  lua_newtable(L);
  // lua_pushstring(L, chidfilebase);
  // lua_setfield(L, -2, "chid");
  lua_pushstring(L, fdsprefix);
  lua_setfield(L, -2, "fdsprefix");

  lua_pushcfunction(L, &lua_case_title);
  lua_setfield(L, -2, "plot_title");

  // TODO: copying the array into lua allows for slightly faster access,
  // but is less ergonomic, leave direct access as the default, with copying
  // in cases where it is shown to be a useful speedup
  // lua_get_global_times(L);
  // global_times is currently on the stack
  // add a metatable to it.
  // first create the table

  // Create "global_times" table
  lua_newtable(L);
  // Create "global_times" metatable
  lua_newtable(L);
  lua_pushcfunction(L, &lua_get_nglobal_times);
  lua_setfield(L, -2, "__len");
  lua_pushcfunction(L, &lua_get_global_time_n);
  lua_setfield(L, -2, "__index");
  // then set the metatable
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "global_times");

  // while the meshes themselve will rarely change, the information about them
  // will change regularly. This is handled by the mesh table.
  lua_get_meshes(L);
  // meshes is currently on the stack
  // add a metatable to it.
  // first create the table
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &lua_get_nmeshes);
  lua_setfield(L, -2, "__len");
  // then set the metatable
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "meshes");

  // As with meshes the number and names of devices is unlikely to change
  lua_get_devices(L);
  // devices is currently on the stack
  // add a metatable to it.
  // first create the table
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &lua_get_ndevices);
  lua_setfield(L, -2, "__len");
  // then set the metatable
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "devices");

  // sliceinfo is a 1-indexed array so the lua length operator
  // works without the need for a metatable
  lua_get_sliceinfo(L);
  lua_setfield(L, -2, "slices");

  // lua_get_rampinfo(L);
  // lua_setglobal(L, "rampinfo");

  lua_get_csvinfo(L);
  // csvinfo is currently on the stack
  // add a metatable to it.
  // first create the table
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &lua_get_ncsvinfo);
  lua_setfield(L, -2, "__len");
  // then set the metatable
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "csvs");

  lua_get_plot3dinfo(L);
  // plot3dinfo is currently on the stack
  // add a metatable to it.
  // first create the table
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &lua_get_nplot3dinfo);
  lua_setfield(L, -2, "__len");
  // then set the metatable
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "pl3ds");

  // set up tables to access pl3dinfo better
  // setup_pl3dtables(L);

  // lua_get_geomdata(L);
  // lua_setglobal(L, "geomdata");

  // Case metatable
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &lua_case_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, &lua_case_newindex);
  lua_setfield(L, -2, "__newindex");
  lua_setmetatable(L, -2);

  return 1;
}

/// @brief As with lua_initsmvdata(), but for information relating to Smokeview
/// itself.
int lua_initsmvproginfo(lua_State *L) {
  char version[256];
  // char githash[256];

  GetProgVersion(version);
  addLuaPaths(L);
  // getGitHash(githash);

  lua_createtable(L, 0, 6);

  lua_pushstring(L, version);
  lua_setfield(L, -2, "version");

  // lua_pushstring(L, githash);
  // lua_setfield(L, -2, "githash");

  lua_pushstring(L, __DATE__);
  lua_setfield(L, -2, "builddate");

  lua_pushstring(L, fds_githash);
  lua_setfield(L, -2, "fdsgithash");

  lua_pushstring(L, smokeviewpath);
  lua_setfield(L, -2, "smokeviewpath");

  lua_pushstring(L, smokezippath);
  lua_setfield(L, -2, "smokezippath");

  lua_pushstring(L, texturedir);
  lua_setfield(L, -2, "texturedir");

  lua_setglobal(L, "smokeviewProgram");
  return 0;
}

int lua_get_slice(lua_State *L) {
  // This should push a lightuserdata onto the stack which is a pointer to the
  // slicedata. This takes the index of the slice (in the sliceinfo array) as an
  // argument.
  // Get the index of the slice as an argument to the lua function.
  int slice_index = lua_tonumber(L, 1);
  // Get the pointer to the slicedata struct.
  slicedata *slice = &sliceinfo[slice_index];
  // Push the pointer onto the lua stack as lightuserdata.
  lua_pushlightuserdata(L, slice);
  // lua_newuserdata places the data on the stack, so return a single stack
  // item.
  return 1;
}

/// @brief This takes a lightuserdata pointer as an argument, and returns the
/// slice label as a string.
int lua_slice_get_label(lua_State *L) {
  // get the lightuserdata from the stack, which is a pointer to the 'slicedata'
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  // Push the string onto the stack
  lua_pushstring(L, slice->slicelabel);
  return 1;
}

/// @brief This takes a lightuserdata pointer as an argument, and returns the
/// slice filename as a string.
int lua_slice_get_filename(lua_State *L) {
  // Get the lightuserdata from the stack, which is a pointer to the
  // 'slicedata'.
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  // Push the string onto the stack
  lua_pushstring(L, slice->file);
  return 1;
}

int lua_slice_get_data(lua_State *L) {
  // get the lightuserdata from the stack, which is a pointer to the 'slicedata'
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  // Push a lightuserdata (a pointer) onto the lua stack that points to the
  // qslicedata.
  lua_pushlightuserdata(L, slice->qslicedata);
  return 1;
}

int lua_slice_get_times(lua_State *L) {
  int i;
  // get the lightuserdata from the stack, which is a pointer to the 'slicedata'
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  // Push a lightuserdata (a pointer) onto the lua stack that points to the
  // qslicedata.
  lua_createtable(L, slice->ntimes, 0);
  for (i = 0; i < slice->ntimes; i++) {
    lua_pushnumber(L, i + 1);
    lua_pushnumber(L, slice->times[i]);
    lua_settable(L, -3);
  }
  return 1;
}

int lua_get_part(lua_State *L) {
  // This should push a lightuserdata onto the stack which is a pointer to the
  // partdata. This takes the index of the part (in the partinfo array) as an
  // argument.
  // Get the index of the slice as an argument to the lua function.
  int part_index = lua_tonumber(L, 1);
  // Get the pointer to the slicedata struct.
  partdata *part = &partinfo[part_index];
  // Push the pointer onto the lua stack as lightuserdata.
  lua_pushlightuserdata(L, part);
  // lua_newuserdata places the data on the stack, so return a single stack
  // item.
  return 1;
}

// pass in the part data
int lua_get_part_npoints(lua_State *L) {
  int index;
  partdata *parti = (partdata *)lua_touserdata(L, 1);
  if (!parti->loaded) {
    return luaL_error(L, "particle file %s not loaded", parti->file);
  }

  // Create a table with an entry for x, y and name
  lua_createtable(L, 3, 0);

  lua_pushstring(L, "name");
  lua_pushstring(L, "(unknown)");
  lua_settable(L, -3);

  // x entries
  lua_pushstring(L, "x");
  lua_createtable(L, 3, 0);

  lua_pushstring(L, "units");
  lua_pushstring(L, "s");
  lua_settable(L, -3);

  lua_pushstring(L, "name");
  lua_pushstring(L, "Time");
  lua_settable(L, -3);

  lua_pushstring(L, "values");
  lua_createtable(L, parti->ntimes, 0);

  // Create a table with an entry for each time
  for (index = 0; index < parti->ntimes; index++) {
    part5data *part5 = parti->data5 + index * parti->nclasses;
    // sum += part5->npoints;

    // use a 1-indexed array to match lua
    lua_pushnumber(L, index + 1);
    lua_pushnumber(L, part5->time);
    lua_settable(L, -3);
  }
  lua_settable(L, -3);
  lua_settable(L, -3);

  // y entries
  lua_pushstring(L, "y");
  lua_createtable(L, 3, 0);

  lua_pushstring(L, "units");
  lua_pushstring(L, "#");
  lua_settable(L, -3);

  lua_pushstring(L, "name");
  lua_pushstring(L, "# Particles");
  lua_settable(L, -3);

  lua_pushstring(L, "values");
  lua_createtable(L, parti->ntimes, 0);

  // Create a table with an entry for each time
  for (index = 0; index < parti->ntimes; index++) {
    part5data *part5 = parti->data5 + index * parti->nclasses;
    // sum += part5->npoints;

    // use a 1-indexed array to match lua
    lua_pushnumber(L, index + 1);
    lua_pushnumber(L, part5->npoints);
    lua_settable(L, -3);
  }
  lua_settable(L, -3);
  lua_settable(L, -3);

  // Return a table of values.
  return 1;
}

// int lua_get_all_part_

int lua_slice_data_map_frames(lua_State *L) {
  // The first argument to this function is the slice pointer. This function
  // receives the values of the slice at a particular frame as an array.
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  if (!slice->loaded) {
    return luaL_error(L, "slice %s not loaded", slice->file);
  }
  int framepoints = slice->nslicex * slice->nslicey;
  // Pointer to the first frame.
  float *qslicedata = slice->qslicedata;
  // The second argument is the function to be called on each frame.
  lua_createtable(L, slice->ntimes, 0);
  // framenumber is the index of the frame (0-based).
  int framenumber;
  for (framenumber = 0; framenumber < slice->ntimes; framenumber++) {
    // duplicate the function so that we can use it and keep it
    lua_pushvalue(L, 2);
    // Push the first frame onto the stack by first putting them into a lua
    // table. Values are indexed from 1.
    // Feed the lua function a lightuserdata (pointer) that is can use
    // with a special function to index the array.
    lua_pushnumber(L, framepoints);
    // lua_pushlightuserdata(L, &qslicedata[framenumber*framepoints]);

    // this table method is more flexible but slower
    lua_createtable(L, framepoints, 0);
    // pointnumber is the index of the data point in the frame (0-based).
    int pointnumber;
    for (pointnumber = 0; pointnumber < framepoints; pointnumber++) {
      // adjust the index to start from 1
      lua_pushnumber(L, pointnumber + 1);
      lua_pushnumber(L, qslicedata[framenumber * framepoints + pointnumber]);
      lua_settable(L, -3);
    }

    // The function takes 2 arguments and returns 1 result.
    lua_call(L, 2, 1);
    // Add the value to the results table.
    lua_pushnumber(L, framenumber + 1);
    lua_pushvalue(L, -2);
    lua_settable(L, -4);
    lua_pop(L, 1);
  }
  // Return a table of values.
  return 1;
}

int lua_slice_data_map_frames_count_less(lua_State *L) {
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  if (!slice->loaded) {
    return luaL_error(L, "slice %s not loaded", slice->file);
  }
  float threshold = lua_tonumber(L, 2);
  int framepoints = slice->nslicex * slice->nslicey;
  // Pointer to the first frame.
  float *qslicedata = slice->qslicedata;
  lua_createtable(L, slice->ntimes, 0);
  int framenumber;
  for (framenumber = 0; framenumber < slice->ntimes; framenumber++) {
    int count = 0;
    int pointnumber;
    for (pointnumber = 0; pointnumber < framepoints; pointnumber++) {
      if (*qslicedata < threshold) {
        count++;
      }
      qslicedata++;
    }
    lua_pushnumber(L, framenumber + 1);
    lua_pushnumber(L, count);
    lua_settable(L, -3);
  }
  // Return a table of values.
  return 1;
}

int lua_slice_data_map_frames_count_less_eq(lua_State *L) {
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  if (!slice->loaded) {
    return luaL_error(L, "slice %s not loaded", slice->file);
  }
  float threshold = lua_tonumber(L, 2);
  int framepoints = slice->nslicex * slice->nslicey;
  // Pointer to the first frame.
  float *qslicedata = slice->qslicedata;
  lua_createtable(L, slice->ntimes, 0);
  int framenumber;
  for (framenumber = 0; framenumber < slice->ntimes; framenumber++) {
    int count = 0;
    int pointnumber;
    for (pointnumber = 0; pointnumber < framepoints; pointnumber++) {
      if (*qslicedata <= threshold) {
        count++;
      }
      qslicedata++;
    }
    lua_pushnumber(L, framenumber + 1);
    lua_pushnumber(L, count);
    lua_settable(L, -3);
  }
  // Return a table of values.
  return 1;
}

int lua_slice_data_map_frames_count_greater(lua_State *L) {
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  if (!slice->loaded) {
    return luaL_error(L, "slice %s not loaded", slice->file);
  }
  float threshold = lua_tonumber(L, 2);
  int framepoints = slice->nslicex * slice->nslicey;
  // Pointer to the first frame.
  float *qslicedata = slice->qslicedata;
  lua_createtable(L, slice->ntimes, 0);
  int framenumber;
  for (framenumber = 0; framenumber < slice->ntimes; framenumber++) {
    int count = 0;
    int pointnumber;
    for (pointnumber = 0; pointnumber < framepoints; pointnumber++) {
      if (*qslicedata > threshold) {
        count++;
      }
      qslicedata++;
    }
    lua_pushnumber(L, framenumber + 1);
    lua_pushnumber(L, count);
    lua_settable(L, -3);
  }
  // Return a table of values.
  return 1;
}

int lua_slice_data_map_frames_count_greater_eq(lua_State *L) {
  slicedata *slice = (slicedata *)lua_touserdata(L, 1);
  if (!slice->loaded) {
    return luaL_error(L, "slice %s not loaded", slice->file);
  }
  float threshold = lua_tonumber(L, 2);
  int framepoints = slice->nslicex * slice->nslicey;
  // Pointer to the first frame.
  float *qslicedata = slice->qslicedata;
  lua_createtable(L, slice->ntimes, 0);
  int framenumber;
  for (framenumber = 0; framenumber < slice->ntimes; framenumber++) {
    int count = 0;
    int pointnumber;
    for (pointnumber = 0; pointnumber < framepoints; pointnumber++) {
      if (*qslicedata >= threshold) {
        count++;
      }
      qslicedata++;
    }
    lua_pushnumber(L, framenumber + 1);
    lua_pushnumber(L, count);
    lua_settable(L, -3);
  }
  // Return a table of values.
  return 1;
}

/// @brief Pushes a value from a slice onto the stack (a single slice, not
/// multi). The arguments are
/// 1. int framenumber
/// 2. int i
/// 3. int j
/// 4. ink k
/// The slice index is stored as part of a closure.
int lua_getslicedata(lua_State *L) {
  // The offset in the global sliceinfo table of the slice.
  int slice_index = lua_tonumber(L, lua_upvalueindex(1));
  // The time frame to use
  int f = lua_tonumber(L, 1);
  // The offsets into the mesh requested (NOT the data array)
  int i = lua_tonumber(L, 2);
  int j = lua_tonumber(L, 3);
  int k = lua_tonumber(L, 4);
  // printf("getting slice data: %d, %d, %d-%d-%d\n", slice_index, f, i, j, k);
  // print all the times
  // printf("times: %d\n", sliceinfo[slice_index].ntimes);
  // int n = 0;
  // for (n; n < sliceinfo[slice_index].ntimes; n++) {
  //   fprintf(stderr, "t:%.2f s\n", sliceinfo[slice_index].times[n]);
  // }
  // fprintf(stderr, "f:%d i:%d j:%d  k:%d\n", f, i,j,k);

  int imax = sliceinfo[slice_index].ijk_max[0];
  int jmax = sliceinfo[slice_index].ijk_max[1];
  int kmax = sliceinfo[slice_index].ijk_max[2];

  int di = sliceinfo[slice_index].nslicei;
  int dj = sliceinfo[slice_index].nslicej;
  int dk = sliceinfo[slice_index].nslicek;
  // Check that the offsets do not exceed the bounds of a single data frame
  if (i > imax || j > jmax || k > kmax) {
    fprintf(stderr, "ERROR: offsets exceed bounds");
    exit(1);
  }
  // Convert the offsets into the mesh into offsets into the data array
  int i_offset = i - sliceinfo[slice_index].ijk_min[0];
  int j_offset = j - sliceinfo[slice_index].ijk_min[1];
  int k_offset = k - sliceinfo[slice_index].ijk_min[2];

  // Offset into a single frame
  int offset = (dk * dj) * i_offset + dk * j_offset + k_offset;
  int framesize = di * dj * dk;
  float val = sliceinfo[slice_index].qslicedata[offset + f * framesize];
  // lua_pushstring(L,sliceinfo[slice_index].file);
  lua_pushnumber(L, val);
  return 1;
}

/// @brief Build a Lua table with information on the slices of the model.
// TODO: change this to use userdata instead
int lua_get_sliceinfo(lua_State *L) {
  lua_createtable(L, 0, nsliceinfo);
  int i;
  for (i = 0; i < nsliceinfo; i++) {
    lua_pushnumber(L, i + 1);
    lua_createtable(L, 0, 21);

    if (sliceinfo[i].slicelabel != NULL) {
      lua_pushstring(L, sliceinfo[i].slicelabel);
      lua_setfield(L, -2, "label");
    }

    lua_pushnumber(L, i);
    lua_setfield(L, -2, "n");

    if (sliceinfo[i].label.longlabel != NULL) {
      lua_pushstring(L, sliceinfo[i].label.longlabel);
      lua_setfield(L, -2, "longlabel");
    }

    if (sliceinfo[i].label.shortlabel != NULL) {
      lua_pushstring(L, sliceinfo[i].label.shortlabel);
      lua_setfield(L, -2, "shortlabel");
    }

    lua_pushstring(L, sliceinfo[i].file);
    lua_setfield(L, -2, "file");

    lua_pushnumber(L, sliceinfo[i].slice_filetype);
    lua_setfield(L, -2, "slicefile_type");

    lua_pushnumber(L, sliceinfo[i].idir);
    lua_setfield(L, -2, "idir");

    lua_pushnumber(L, sliceinfo[i].sliceoffset);
    lua_setfield(L, -2, "sliceoffset");

    lua_pushnumber(L, sliceinfo[i].ijk_min[0]);
    lua_setfield(L, -2, "imin");

    lua_pushnumber(L, sliceinfo[i].ijk_max[0]);
    lua_setfield(L, -2, "imax");

    lua_pushnumber(L, sliceinfo[i].ijk_min[1]);
    lua_setfield(L, -2, "jmin");

    lua_pushnumber(L, sliceinfo[i].ijk_max[1]);
    lua_setfield(L, -2, "jmax");

    lua_pushnumber(L, sliceinfo[i].ijk_min[2]);
    lua_setfield(L, -2, "kmin");

    lua_pushnumber(L, sliceinfo[i].ijk_max[2]);
    lua_setfield(L, -2, "kmax");

    lua_pushnumber(L, sliceinfo[i].blocknumber);
    lua_setfield(L, -2, "blocknumber");

    lua_pushnumber(L, sliceinfo[i].position_orig);
    lua_setfield(L, -2, "position_orig");

    lua_pushnumber(L, sliceinfo[i].nslicex);
    lua_setfield(L, -2, "nslicex");

    lua_pushnumber(L, sliceinfo[i].nslicey);
    lua_setfield(L, -2, "nslicey");

    lua_pushstring(L, sliceinfo[i].cdir);
    lua_setfield(L, -2, "slicedir");

    // can't be done until loaded
    // lua_pushnumber(L, sliceinfo[i].ntimes);
    // lua_setfield(L, -2, "ntimes");

    // Push the slice index so that getslicedata knows which slice to operate
    // on.
    lua_pushnumber(L, i);
    // Push a closure which has been provided with the first argument (the slice
    // index)
    lua_pushcclosure(L, lua_getslicedata, 1);
    lua_setfield(L, -2, "getdata");

    lua_settable(L, -3);
  }
  return 1;
}

int lua_get_csventry(lua_State *L) {
  const char *key = lua_tostring(L, -1);
  csvfiledata *csventry = get_csvinfo(key);
  int index = get_csvindex(key);
  lua_createtable(L, 0, 4);
  lua_pushstring(L, csventry->file);
  lua_setfield(L, -2, "file");

  lua_pushstring(L, csventry->c_type);
  lua_setfield(L, -2, "c_type");

  lua_pushnumber(L, index);
  lua_setfield(L, -2, "index");

  lua_pushstring(L, key);
  lua_pushcclosure(L, &lua_load_csv, 1);
  lua_setfield(L, -2, "load");

  // Create a metatable.
  // TODO: this metatable might be more easily implemented directly in Lua.
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &access_csventry_prop);
  lua_setfield(L, -2, "__index");
  // then set the metatable
  lua_setmetatable(L, -2);
  return 1;
}

/// @brief Build a Lua table with information on the CSV files available to the
/// model.
// TODO: provide more information via this interface.
// TODO: use metatables so that the most up-to-date information is retrieved.
int lua_get_csvinfo(lua_State *L) {
  lua_createtable(L, 0, ncsvfileinfo);
  int i;
  for (i = 0; i < ncsvfileinfo; i++) {
    lua_pushstring(L, csvfileinfo[i].c_type);
    lua_get_csventry(L);
    lua_settable(L, -3);
  }
  return 1;
}

int lua_loadvslice(lua_State *L) {
  const char *type = lua_tostring(L, 1);
  int axis = lua_tonumber(L, 2);
  float distance = lua_tonumber(L, 3);
  loadvslice(type, axis, distance);
  return 0;
}

int lua_loadiso(lua_State *L) {
  const char *type = lua_tostring(L, 1);
  loadiso(type);
  return 0;
}

int lua_load3dsmoke(lua_State *L) {
  const char *smoke_type = lua_tostring(L, 1);
  load3dsmoke(smoke_type);
  return 0;
}

int lua_loadvolsmoke(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  loadvolsmoke(meshnumber);
  return 0;
}

int lua_loadvolsmokeframe(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  int framenumber = lua_tonumber(L, 1);
  loadvolsmokeframe(meshnumber, framenumber, 1);
  // returnval = 1; // TODO: determine if this is the correct behaviour.
  // this is what is done in the SSF code.
  return 0;
}

/// @brief Set the format of images which will be exported. The value should be
/// a string. The acceptable values are:
///   "JPG"
///   "PNG"
int lua_set_rendertype(lua_State *L) {
  const char *type = lua_tostring(L, 1);
  if (set_rendertype(type)) {
    return luaL_error(L, "%s is not a valid render type", type);
  }
  return 0;
}

int lua_get_rendertype(lua_State *L) {
  int render_type = get_rendertype();
  switch (render_type) {
  case JPEG:
    lua_pushstring(L, "JPG");
    break;
  case PNG:
    lua_pushstring(L, "PNG");
    break;
  default:
    lua_pushstring(L, NULL);
    break;
  }
  return 1;
}

/// @brief Set the format of movies which will be exported. The value should be
/// a string. The acceptable values are:
///    - "WMV"
///    - "MP4"
///    - "AVI"
int lua_set_movietype(lua_State *L) {
  const char *type = lua_tostring(L, 1);
  set_movietype(type);
  return 0;
}

int lua_get_movietype(lua_State *L) {
  int movie_type = get_movietype();
  switch (movie_type) {
  case WMV:
    lua_pushstring(L, "WMV");
    break;
  case MP4:
    lua_pushstring(L, "MP4");
    break;
  case AVI:
    lua_pushstring(L, "AVI");
    break;
  default:
    lua_pushstring(L, NULL);
    break;
  }
  return 1;
}

int lua_makemovie(lua_State *L) {
  const char *name = lua_tostring(L, 1);
  const char *base = lua_tostring(L, 2);
  float framerate = lua_tonumber(L, 3);
  makemovie(name, base, framerate);
  return 0;
}

int lua_loadtour(lua_State *L) {
  const char *name = lua_tostring(L, 1);
  int error_code = loadtour(name);
  lua_pushnumber(L, error_code);
  return 1;
}

int lua_loadparticles(lua_State *L) {
  const char *name = lua_tostring(L, 1);
  loadparticles(name);
  return 0;
}

int lua_partclasscolor(lua_State *L) {
  const char *color = lua_tostring(L, 1);
  partclasscolor(color);
  return 0;
}

int lua_partclasstype(lua_State *L) {
  const char *type = lua_tostring(L, 1);
  partclasstype(type);
  return 0;
}

int lua_plot3dprops(lua_State *L) {
  int variable_index = lua_tonumber(L, 1);
  int showvector = lua_toboolean(L, 2);
  int vector_length_index = lua_tonumber(L, 3);
  int display_type = lua_tonumber(L, 4);
  float vector_length = lua_tonumber(L, 5);
  plot3dprops(variable_index, showvector, vector_length_index, display_type,
              vector_length);
  return 0;
}

int lua_loadplot3d(lua_State *L) {
  int meshnumber = lua_tonumber(L, 1);
  float time_local = lua_tonumber(L, 2);
  loadplot3d(meshnumber, time_local);
  return 0;
}

int lua_unloadall(lua_State *L) {
  unloadall();
  return 0;
}

int lua_unloadtour(lua_State *L) {
  unloadtour();
  return 0;
}

int lua_setrenderdir(lua_State *L) {
  const char *dir = lua_tostring(L, -1);
  int return_code = setrenderdir(dir);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_getrenderdir(lua_State *L) {
  lua_pushstring(L, script_dir_path);
  return 1;
}

int lua_set_ortho_preset(lua_State *L) {
  const char *viewpoint = lua_tostring(L, 1);
  int errorcode = set_ortho_preset(viewpoint);
  lua_pushnumber(L, errorcode);
  return 1;
}

int lua_setviewpoint(lua_State *L) {
  const char *viewpoint = lua_tostring(L, 1);
  int errorcode = setviewpoint(viewpoint);
  lua_pushnumber(L, errorcode);
  return 1;
}

int lua_getviewpoint(lua_State *L) {
  lua_pushstring(L, camera_current->name);
  return 1;
}

int lua_exit_smokeview(lua_State *L) {
  exit_smokeview();
  return 0;
}

int lua_setwindowsize(lua_State *L) {
  int width = lua_tonumber(L, 1);
  int height = lua_tonumber(L, 2);
  setwindowsize(width, height);
  // Using the DisplayCB is not sufficient in this case,
  // control must be temporarily returned to the main glut loop.
  lua_tempyieldscript(L);
  return 0;
}

int lua_setgridvisibility(lua_State *L) {
  int selection = lua_tonumber(L, 1);
  setgridvisibility(selection);
  return 0;
}

int lua_setgridparms(lua_State *L) {
  int x_vis = lua_tonumber(L, 1);
  int y_vis = lua_tonumber(L, 2);
  int z_vis = lua_tonumber(L, 3);

  int x_plot = lua_tonumber(L, 4);
  int y_plot = lua_tonumber(L, 5);
  int z_plot = lua_tonumber(L, 6);

  setgridparms(x_vis, y_vis, z_vis, x_plot, y_plot, z_plot);

  return 0;
}

int lua_setcolorbarflip(lua_State *L) {
  int flip = lua_toboolean(L, 1);
  setcolorbarflip(flip);
  lua_tempyieldscript(L);
  return 0;
}

int lua_getcolorbarflip(lua_State *L) {
  int flip = getcolorbarflip();
  lua_pushboolean(L, flip);
  return 1;
}

int lua_setcolorbarindex(lua_State *L) {
  int chosen_index = lua_tonumber(L, 1);
  setcolorbarindex(chosen_index);
  return 0;
}

int lua_getcolorbarindex(lua_State *L) {
  int index = getcolorbarindex();
  lua_pushnumber(L, index);
  return 1;
}

int lua_set_slice_in_obst(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_slice_in_obst(setting);
  return 0;
}

int lua_get_slice_in_obst(lua_State *L) {
  int setting = get_slice_in_obst();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_set_colorbar(lua_State *L) {
  int index = lua_tonumber(L, 1);
  set_colorbar(index);
  return 0;
}

int lua_set_named_colorbar(lua_State *L) {
  const char *name = lua_tostring(L, 1);
  int err = set_named_colorbar(name);
  if (err == 1) {
    luaL_error(L, "%s is not a valid colorbar name", name);
  }
  return 0;
}

// int lua_get_named_colorbar(lua_State *L) {
//   int err = get_named_colorbar();
//   if (err == 1) {
//     luaL_error(L, "%s is not a valid colorbar name", name);
//   }
//   return 0;
// }

//////////////////////

int lua_set_colorbar_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_colorbar_visibility(setting);
  return 0;
}

int lua_get_colorbar_visibility(lua_State *L) {
  int setting = get_colorbar_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_colorbar_visibility(lua_State *L) {
  toggle_colorbar_visibility();
  return 0;
}

int lua_set_colorbar_visibility_horizontal(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_colorbar_visibility_horizontal(setting);
  return 0;
}

int lua_get_colorbar_visibility_horizontal(lua_State *L) {
  int setting = get_colorbar_visibility_horizontal();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_colorbar_visibility_horizontal(lua_State *L) {
  toggle_colorbar_visibility_horizontal();
  return 0;
}

int lua_set_colorbar_visibility_vertical(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_colorbar_visibility_vertical(setting);
  return 0;
}

int lua_get_colorbar_visibility_vertical(lua_State *L) {
  int setting = get_colorbar_visibility_vertical();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_colorbar_visibility_vertical(lua_State *L) {
  toggle_colorbar_visibility_vertical();
  return 0;
}

int lua_set_timebar_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_timebar_visibility(setting);
  return 0;
}

int lua_get_timebar_visibility(lua_State *L) {
  int setting = get_timebar_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_timebar_visibility(lua_State *L) {
  toggle_timebar_visibility();
  return 0;
}

// title
int lua_set_title_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_title_visibility(setting);
  return 0;
}

int lua_get_title_visibility(lua_State *L) {
  int setting = get_title_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_title_visibility(lua_State *L) {
  toggle_title_visibility();
  return 0;
}

// smv_version
int lua_set_smv_version_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_smv_version_visibility(setting);
  return 0;
}

int lua_get_smv_version_visibility(lua_State *L) {
  int setting = get_smv_version_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_smv_version_visibility(lua_State *L) {
  toggle_smv_version_visibility();
  return 0;
}

// chid
int lua_set_chid_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_chid_visibility(setting);
  return 0;
}

int lua_get_chid_visibility(lua_State *L) {
  int setting = get_chid_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_chid_visibility(lua_State *L) {
  toggle_chid_visibility();
  return 0;
}

// blockages
int lua_blockages_hide_all(lua_State *L) {
  blockages_hide_all();
  return 0;
}

// outlines
int lua_outlines_hide(lua_State *L) {
  outlines_hide();
  return 0;
}
int lua_outlines_show(lua_State *L) {
  outlines_show();
  return 0;
}

// surfaces
int lua_surfaces_hide_all(lua_State *L) {
  surfaces_hide_all();
  return 0;
}

// devices
int lua_devices_hide_all(lua_State *L) {
  devices_hide_all();
  return 0;
}

// axis
int lua_set_axis_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_axis_visibility(setting);
  return 0;
}

int lua_get_axis_visibility(lua_State *L) {
  int setting = get_axis_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_axis_visibility(lua_State *L) {
  toggle_axis_visibility();
  return 0;
}

// frame
int lua_set_framelabel_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_framelabel_visibility(setting);
  return 0;
}

int lua_get_framelabel_visibility(lua_State *L) {
  int setting = get_framelabel_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_framelabel_visibility(lua_State *L) {
  toggle_framelabel_visibility();
  return 0;
}

// framerate
int lua_set_framerate_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_framerate_visibility(setting);
  return 0;
}

int lua_get_framerate_visibility(lua_State *L) {
  int setting = get_framerate_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_framerate_visibility(lua_State *L) {
  toggle_framerate_visibility();
  return 0;
}

// grid locations
int lua_set_gridloc_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_gridloc_visibility(setting);
  return 0;
}

int lua_get_gridloc_visibility(lua_State *L) {
  int setting = get_gridloc_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_gridloc_visibility(lua_State *L) {
  toggle_gridloc_visibility();
  return 0;
}

// hrrpuv cutoff
int lua_set_hrrcutoff_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_hrrcutoff_visibility(setting);
  return 0;
}

int lua_get_hrrcutoff_visibility(lua_State *L) {
  int setting = get_hrrcutoff_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_hrrcutoff_visibility(lua_State *L) {
  toggle_hrrcutoff_visibility();
  return 0;
}

// HRR Label Visbility
int lua_set_hrrlabel_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_hrrlabel_visibility(setting);
  return 0;
}

int lua_get_hrrlabel_visibility(lua_State *L) {
  int setting = get_hrrlabel_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_hrrlabel_visibility(lua_State *L) {
  toggle_hrrlabel_visibility();
  return 0;
}
// memory load
#ifdef pp_memstatus
int lua_set_memload_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_memload_visibility(setting);
  return 0;
}

int lua_get_memload_visibility(lua_State *L) {
  int setting = get_memload_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_memload_visibility(lua_State *L) {
  toggle_memload_visibility();
  return 0;
}
#endif

// mesh label
int lua_set_meshlabel_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_meshlabel_visibility(setting);
  return 0;
}

int lua_get_meshlabel_visibility(lua_State *L) {
  int setting = get_meshlabel_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_meshlabel_visibility(lua_State *L) {
  toggle_meshlabel_visibility();
  return 0;
}

// slice average
int lua_set_slice_average_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_slice_average_visibility(setting);
  return 0;
}

int lua_get_slice_average_visibility(lua_State *L) {
  int setting = get_slice_average_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_slice_average_visibility(lua_State *L) {
  toggle_slice_average_visibility();
  return 0;
}

// time
int lua_set_time_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_time_visibility(setting);
  return 0;
}

int lua_get_time_visibility(lua_State *L) {
  int setting = get_time_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_time_visibility(lua_State *L) {
  toggle_time_visibility();
  return 0;
}

// user settable ticks
int lua_set_user_ticks_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_user_ticks_visibility(setting);
  return 0;
}

int lua_get_user_ticks_visibility(lua_State *L) {
  int setting = get_user_ticks_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_user_ticks_visibility(lua_State *L) {
  toggle_user_ticks_visibility();
  return 0;
}

// version info
int lua_set_version_info_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_version_info_visibility(setting);
  return 0;
}

int lua_get_version_info_visibility(lua_State *L) {
  int setting = get_version_info_visibility();
  lua_pushboolean(L, setting);
  return 1;
}

int lua_toggle_version_info_visibility(lua_State *L) {
  toggle_version_info_visibility();
  return 0;
}

// set all
int lua_set_all_label_visibility(lua_State *L) {
  int setting = lua_toboolean(L, 1);
  set_all_label_visibility(setting);
  return 0;
}

//////////////////////////////////////

int lua_blockage_view_method(lua_State *L) {
  int setting = lua_tonumber(L, 1);
  int return_code = blockage_view_method(setting);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_blockage_outline_color(lua_State *L) {
  int setting = lua_tonumber(L, 1);
  int return_code = blockage_outline_color(setting);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_blockage_locations(lua_State *L) {
  int setting = lua_tonumber(L, 1);
  int return_code = blockage_locations(setting);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_camera_mod_eyex(lua_State *L) {
  float delta = lua_tonumber(L, 1);
  camera_mod_eyex(delta);
  return 0;
}

int lua_camera_set_eyex(lua_State *L) {
  float eyex = lua_tonumber(L, 1);
  camera_set_eyex(eyex);
  return 0;
}

int lua_camera_mod_eyey(lua_State *L) {
  float delta = lua_tonumber(L, 1);
  camera_mod_eyey(delta);
  return 0;
}

int lua_camera_set_eyey(lua_State *L) {
  float eyey = lua_tonumber(L, 1);
  camera_set_eyey(eyey);
  return 0;
}

int lua_camera_mod_eyez(lua_State *L) {
  float delta = lua_tonumber(L, 1);
  camera_mod_eyez(delta);
  return 0;
}

int lua_camera_set_eyez(lua_State *L) {
  float eyez = lua_tonumber(L, 1);
  camera_set_eyez(eyez);
  return 0;
}

int lua_camera_mod_az(lua_State *L) {
  float delta = lua_tonumber(L, 1);
  camera_mod_az(delta);
  return 0;
}

int lua_camera_set_az(lua_State *L) {
  float az = lua_tonumber(L, 1);
  camera_set_az(az);
  return 0;
}

int lua_camera_get_az(lua_State *L) {
  lua_pushnumber(L, camera_get_az());
  return 1;
}

int lua_camera_mod_elev(lua_State *L) {
  float delta = lua_tonumber(L, 1);
  camera_mod_elev(delta);
  return 0;
}
int lua_camera_zoom_to_fit(lua_State *L) {
  camera_zoom_to_fit();
  return 0;
}

int lua_camera_set_elev(lua_State *L) {
  float elev = lua_tonumber(L, 1);
  camera_set_elev(elev);
  return 0;
}

int lua_camera_get_elev(lua_State *L) {
  lua_pushnumber(L, camera_get_elev());
  return 1;
}
int lua_camera_get_projection_type(lua_State *L) {
  float projection_type = camera_get_projection_type();
  lua_pushnumber(L, projection_type);
  return 1;
}
int lua_camera_set_projection_type(lua_State *L) {
  float projection_type = lua_tonumber(L, 1);
  int return_value = camera_set_projection_type(projection_type);
  lua_pushnumber(L, return_value);
  return 1;
}

int lua_camera_get_rotation_type(lua_State *L) {
  float rotation_type = camera_get_rotation_type();
  lua_pushnumber(L, rotation_type);
  return 1;
}

int lua_camera_get_rotation_index(lua_State *L) {
  float rotation_index = camera_get_rotation_index();
  lua_pushnumber(L, rotation_index);
  return 1;
}

int lua_camera_set_rotation_type(lua_State *L) {
  int rotation_type = lua_tonumber(L, 1);
  camera_set_rotation_type(rotation_type);
  return 0;
}

int lua_camera_get_zoom(lua_State *L) {
  lua_pushnumber(L, zoom);
  return 1;
}

int lua_camera_set_zoom(lua_State *L) {
  float x = lua_tonumber(L, 1);
  zoom = x;
  return 0;
}

int lua_camera_get_eyex(lua_State *L) {
  float eyex = camera_get_eyex();
  lua_pushnumber(L, eyex);
  return 1;
}

int lua_camera_get_eyey(lua_State *L) {
  float eyey = camera_get_eyex();
  lua_pushnumber(L, eyey);
  return 1;
}

int lua_camera_get_eyez(lua_State *L) {
  float eyez = camera_get_eyez();
  lua_pushnumber(L, eyez);
  return 1;
}
int lua_camera_set_viewdir(lua_State *L) {
  float xcen = lua_tonumber(L, 1);
  float ycen = lua_tonumber(L, 2);
  float zcen = lua_tonumber(L, 3);
  camera_set_viewdir(xcen, ycen, zcen);
  return 0;
}

int lua_camera_get_viewdir(lua_State *L) {
  float xcen = camera_get_xcen();
  float ycen = camera_get_ycen();
  float zcen = camera_get_zcen();

  lua_createtable(L, 0, 3);

  lua_pushstring(L, "x");
  lua_pushnumber(L, xcen);
  lua_settable(L, -3);

  lua_pushstring(L, "y");
  lua_pushnumber(L, ycen);
  lua_settable(L, -3);

  lua_pushstring(L, "z");
  lua_pushnumber(L, zcen);
  lua_settable(L, -3);

  return 1;
}

int lua_set_slice_bounds(lua_State *L) {
  const char *slice_type = lua_tostring(L, 1);
  int set_min = lua_tonumber(L, 2);
  float value_min = lua_tonumber(L, 3);
  int set_max = lua_tonumber(L, 4);
  float value_max = lua_tonumber(L, 5);
  set_slice_bounds(slice_type, set_min, value_min, set_max, value_max);
  return 0;
}
int lua_set_slice_bound_min(lua_State *L) {
  const char *slice_type = lua_tostring(L, 1);
  int set = lua_toboolean(L, 2);
  float value = lua_tonumber(L, 3);
  set_slice_bound_min(slice_type, set, value);
  return 0;
}

int lua_get_slice_bounds(lua_State *L) {
  const char *slice_type = lua_tostring(L, 1);
  simple_bounds bounds;
  if (get_slice_bounds(slice_type, &bounds)) {
    luaL_error(L, "Could not get slice bounds for %s", slice_type);
  }
  lua_pushnumber(L, bounds.min);
  lua_pushnumber(L, bounds.max);
  return 2;
}

int lua_set_slice_bound_max(lua_State *L) {
  const char *slice_type = lua_tostring(L, 1);
  int set = lua_toboolean(L, 2);
  float value = lua_tonumber(L, 3);
  set_slice_bound_max(slice_type, set, value);
  return 0;
}

int lua_set_ambientlight(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_ambientlight(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_get_backgroundcolor(lua_State *L) {
  lua_createtable(L, 0, 3);

  lua_pushnumber(L, backgroundbasecolor[0]);
  lua_setfield(L, -2, "r");

  lua_pushnumber(L, backgroundbasecolor[1]);
  lua_setfield(L, -2, "g");

  lua_pushnumber(L, backgroundbasecolor[2]);
  lua_setfield(L, -2, "b");

  return 1;
}

int lua_set_backgroundcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_backgroundcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_blockcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_blockcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_blockshininess(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_blockshininess(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_blockspecular(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_blockspecular(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_boundcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_boundcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

// int lua_set_colorbar_textureflag(lua_State *L) {
//   int setting = lua_tonumber(L, 1);
//   int return_code = set_colorbar_textureflag(setting);
//   lua_pushnumber(L, 1);
//   return 1;
// }

float getcolorfield(lua_State *L, int stack_index, const char *key) {
  if (!lua_istable(L, stack_index)) {
    fprintf(stderr,
            "stack is not a table at index, cannot use getcolorfield\n");
    exit(1);
  }
  // if stack index is relative (negative) convert to absolute (positive)
  if (stack_index < 0) {
    stack_index = lua_gettop(L) + stack_index + 1;
  }
  lua_pushstring(L, key);
  lua_gettable(L, stack_index);
  float result = lua_tonumber(L, -1);
  lua_pop(L, 1);
  return result;
}

int get_color(lua_State *L, int stack_index, float *color) {
  if (!lua_istable(L, stack_index)) {
    fprintf(stderr, "color table is not present\n");
    return 1;
  }
  float r = getcolorfield(L, stack_index, "r");
  float g = getcolorfield(L, stack_index, "g");
  float b = getcolorfield(L, stack_index, "b");
  color[0] = r;
  color[1] = g;
  color[2] = b;
  return 0;
}
int lua_set_colorbar_colors(lua_State *L) {
  printf("running: lua_set_colorbar_colors\n");
  if (!lua_istable(L, 1)) {
    fprintf(stderr, "colorbar table is not present\n");
    return 1;
  }
  int ncolors = 0;
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    ncolors++;
    lua_pop(L, 1);
  }
  if (ncolors > 0) {
    float *colors = malloc(sizeof(float) * ncolors * 3);
    for (int i = 1; i <= ncolors; i++) {
      lua_pushnumber(L, i);
      lua_gettable(L, 1);
      get_color(L, -1, &colors[i - 1]);
    }

    int return_code = set_colorbar_colors(ncolors, colors);
    lua_pushnumber(L, return_code);
    free(colors);
    return 1;
  } else {
    return 0;
  }
}

int lua_get_colorbar_colors(lua_State *L) {
  int i;
  float *rgb_ini_copy_p = rgb_ini;
  lua_createtable(L, 0, nrgb_ini);
  for (i = 0; i < nrgb_ini; i++) {
    lua_pushnumber(L, i + 1);
    lua_createtable(L, 0, 2);

    lua_pushnumber(L, *rgb_ini_copy_p);
    lua_setfield(L, -2, "r");

    lua_pushnumber(L, *(rgb_ini_copy_p + 1));
    lua_setfield(L, -2, "g");

    lua_pushnumber(L, *(rgb_ini_copy_p + 2));
    lua_setfield(L, -2, "b");

    lua_settable(L, -3);
    rgb_ini_copy_p += 3;
  }
  // Leaves one returned value on the stack, the mesh table.
  return 1;
}

int lua_set_color2bar_colors(lua_State *L) {
  int ncolors = lua_tonumber(L, 1);
  if (!lua_istable(L, -1)) {
    fprintf(stderr, "colorbar table is not present\n");
    return 1;
  }
  if (ncolors > 0) {
    float *colors = malloc(sizeof(float) * ncolors * 3);
    for (size_t i = 1; i <= ncolors; i++) {
      lua_pushnumber(L, i);
      lua_gettable(L, -2);
      get_color(L, -1, &colors[i - 1]);
    }

    int return_code = set_color2bar_colors(ncolors, colors);
    lua_pushnumber(L, return_code);
    free(colors);
    return 1;
  } else {
    return 0;
  }
}

int lua_get_color2bar_colors(lua_State *L) {
  int i;
  float *rgb_ini_copy_p = rgb2_ini;
  lua_createtable(L, 0, nrgb2_ini);
  for (i = 0; i < nrgb2_ini; i++) {
    lua_pushnumber(L, i + 1);
    lua_createtable(L, 0, 2);

    lua_pushnumber(L, *rgb_ini_copy_p);
    lua_setfield(L, -2, "r");

    lua_pushnumber(L, *(rgb_ini_copy_p + 1));
    lua_setfield(L, -2, "g");

    lua_pushnumber(L, *(rgb_ini_copy_p + 2));
    lua_setfield(L, -2, "b");

    lua_settable(L, -3);
    rgb_ini_copy_p += 3;
  }
  // Leaves one returned value on the stack, the mesh table.
  return 1;
}

int lua_set_diffuselight(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_diffuselight(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_directioncolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_directioncolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_flip(lua_State *L) {
  int v = lua_toboolean(L, 1);
  int return_code = set_flip(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_get_flip(lua_State *L) {
  lua_pushboolean(L, get_flip());
  return 1;
}

int lua_get_foregroundcolor(lua_State *L) {
  lua_createtable(L, 0, 3);

  lua_pushnumber(L, foregroundbasecolor[0]);
  lua_setfield(L, -2, "r");

  lua_pushnumber(L, foregroundbasecolor[1]);
  lua_setfield(L, -2, "g");

  lua_pushnumber(L, foregroundbasecolor[2]);
  lua_setfield(L, -2, "b");

  return 1;
}

int lua_set_foregroundcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_foregroundcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_heatoffcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_heatoffcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_heatoncolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_heatoncolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_isocolors(lua_State *L) {
  float shininess = lua_tonumber(L, 1);
  float transparency = lua_tonumber(L, 2);
  int transparency_option = lua_tonumber(L, 3);
  int opacity_change = lua_tonumber(L, 4);
  float specular[3];
  get_color(L, 5, specular);
  int n_colors = 0;
  // count the number of colours
  lua_pushnil(L); /* first key */
  while (lua_next(L, 6) != 0) {
    lua_pop(L, 1); // remove value (leave key for next iteration)
    n_colors++;
  }
  int i;
  float colors[MAX_ISO_COLORS][4];
  for (i = 1; i <= n_colors; i++) {
    if (!lua_istable(L, 6)) {
      fprintf(stderr, "isocolor table is not present\n");
      return 1;
    }
    lua_pushnumber(L, i);
    lua_gettable(L, 6);
    get_color(L, -1, colors[i - 1]);
  }
  int return_code = set_isocolors(shininess, transparency, transparency_option,
                                  opacity_change, specular, n_colors, colors);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_colortable(lua_State *L) {
  // int ncolors = lua_tonumber(L, 1);
  int ncolors = 0;
  int i = 0;
  // count the number of colours
  lua_pushnil(L); /* first key */
  while (lua_next(L, 1) != 0) {
    lua_pop(L, 1); // remove value (leave key for next iteration)
    ncolors++;
  }
  if (ncolors > 0) {
    // initialise arrays using the above count info
    float *colors = malloc(sizeof(float) * ncolors * 3);
    // char *names = malloc(sizeof(char)*ncolors*255);
    // char **names = malloc(sizeof(char*));
    /* table is in the stack at index 't' */
    lua_pushnil(L); /* first key */
    while (lua_next(L, 1) != 0) {
      /* uses 'key' (at index -2) and 'value' (at index -1) */
      // strncpy(names[i], lua_tostring(L, -2), 255);
      get_color(L, -1, &colors[i]);
      /* removes 'value'; keeps 'key' for next iteration */
      lua_pop(L, 1);
      i++;
    }
    free(colors);
    // free(names);
  }
  return 0;
}

int lua_set_lightpos0(lua_State *L) {
  float a = lua_tonumber(L, 1);
  float b = lua_tonumber(L, 2);
  float c = lua_tonumber(L, 3);
  float d = lua_tonumber(L, 4);
  int return_code = set_lightpos0(a, b, c, d);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_lightpos1(lua_State *L) {
  float a = lua_tonumber(L, 1);
  float b = lua_tonumber(L, 2);
  float c = lua_tonumber(L, 3);
  float d = lua_tonumber(L, 4);
  int return_code = set_lightpos1(a, b, c, d);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sensorcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_sensorcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sensornormcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_sensornormcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_bw(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int return_code = set_bw(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sprinkleroffcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_sprinkleroffcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sprinkleroncolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_sprinkleroncolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_staticpartcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_staticpartcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_timebarcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_timebarcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_ventcolor(lua_State *L) {
  float r = lua_tonumber(L, 1);
  float g = lua_tonumber(L, 2);
  float b = lua_tonumber(L, 3);
  int return_code = set_ventcolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_gridlinewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_gridlinewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_isolinewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_isolinewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_isopointsize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_isopointsize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_linewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_linewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_partpointsize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_partpointsize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_plot3dlinewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_plot3dlinewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_plot3dpointsize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_plot3dpointsize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sensorabssize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_sensorabssize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sensorrelsize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_sensorrelsize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sliceoffset(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_sliceoffset(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smoothlines(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_smoothlines(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_spheresegs(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_spheresegs(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sprinklerabssize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_sprinklerabssize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_streaklinewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_streaklinewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_ticklinewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_ticklinewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_usenewdrawface(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_usenewdrawface(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_veclength(lua_State *L) {
  float vf = lua_tonumber(L, 1);
  int vec_uniform_length = lua_tonumber(L, 2);
  int vec_uniform_spacing = lua_tonumber(L, 3);
  int return_code = set_veclength(vf, vec_uniform_length, vec_uniform_spacing);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_vectorlinewidth(lua_State *L) {
  float a = lua_tonumber(L, 1);
  float b = lua_tonumber(L, 2);
  int return_code = set_vectorlinewidth(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_vectorpointsize(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_vectorpointsize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_ventlinewidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_ventlinewidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_ventoffset(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_ventoffset(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_windowoffset(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_windowoffset(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_windowwidth(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_windowwidth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_windowheight(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_windowheight(v);
  lua_pushnumber(L, return_code);
  return 1;
}

// --  *** DATA LOADING ***

int lua_set_boundzipstep(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_boundzipstep(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_fed(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_fed(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_fedcolorbar(lua_State *L) {
  const char *name = lua_tostring(L, 1);
  int return_code = set_fedcolorbar(name);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_isozipstep(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_isozipstep(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_nopart(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_nopart(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showfedarea(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showfedarea(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sliceaverage(lua_State *L) {
  int flag = lua_tonumber(L, 1);
  float interval = lua_tonumber(L, 2);
  int vis = lua_tonumber(L, 3);
  int return_code = set_sliceaverage(flag, interval, vis);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_slicedataout(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_slicedataout(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_slicezipstep(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_slicezipstep(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smoke3dzipstep(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_smoke3dzipstep(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_userrotate(lua_State *L) {
  int index = lua_tonumber(L, 1);
  int show_center = lua_tonumber(L, 2);
  float x = lua_tonumber(L, 3);
  float y = lua_tonumber(L, 4);
  float z = lua_tonumber(L, 5);
  int return_code = set_userrotate(index, show_center, x, y, z);
  lua_pushnumber(L, return_code);
  return 1;
}

// --  *** VIEW PARAMETERS ***
int lua_set_aperture(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_aperture(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_blocklocation(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_blocklocation(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_boundarytwoside(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_boundarytwoside(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_clip(lua_State *L) {
  float v_near = lua_tonumber(L, 1);
  float v_far = lua_tonumber(L, 2);
  int return_code = set_clip(v_near, v_far);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_contourtype(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_contourtype(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_cullfaces(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_cullfaces(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_texturelighting(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_texturelighting(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_eyeview(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_eyeview(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_eyex(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_eyex(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_eyey(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_eyey(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_eyez(lua_State *L) {
  float v = lua_tonumber(L, 1);
  int return_code = set_eyez(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_fontsize(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_fontsize(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_frameratevalue(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_frameratevalue(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showfaces_solid(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showfaces_solid(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showfaces_outline(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showfaces_outline(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smoothgeomnormal(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_smoothgeomnormal(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_geomvertexag(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_geomvertexag(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_gversion(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_gversion(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_isotran2(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_isotran2(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_meshvis(lua_State *L) {
  int n = 0;
  int i = 0;
  // count the number of values
  lua_pushnil(L);
  while (lua_next(L, 1) != 0) {
    lua_pop(L, 1); // remove value (leave key for next iteration)
    n++;
  }
  if (n > 0) {
    // initialise arrays using the above count info
    int *vals = malloc(sizeof(int) * n);
    /* table is in the stack at index 't' */
    lua_pushnil(L); /* first key */
    while (lua_next(L, 1) != 0) {
      vals[i] = lua_tonumber(L, -2);
      /* removes 'value'; keeps 'key' for next iteration */
      lua_pop(L, 1);
      i++;
    }
    int return_code = set_meshvis(n, vals);
    lua_pushnumber(L, return_code);
    return 1;
  } else {
    return 0;
  }
}

int lua_set_meshoffset(lua_State *L) {
  int meshnum = lua_tonumber(L, 1);
  int value = lua_tonumber(L, 2);
  int return_code = set_meshoffset(meshnum, value);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_northangle(lua_State *L) {
  int vis = lua_tonumber(L, 1);
  float x = lua_tonumber(L, 2);
  float y = lua_tonumber(L, 3);
  float z = lua_tonumber(L, 4);
  int return_code = set_northangle(vis, x, y, z);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_offsetslice(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_offsetslice(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_outlinemode(lua_State *L) {
  int highlight = lua_tonumber(L, 1);
  int outline = lua_tonumber(L, 2);
  int return_code = set_outlinemode(highlight, outline);
  lua_pushnumber(L, return_code);
  ;
  return 1;
}

int lua_set_p3dsurfacetype(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_p3dsurfacetype(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_p3dsurfacesmooth(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_p3dsurfacesmooth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_scaledfont(lua_State *L) {
  int height2d = lua_tonumber(L, 1);
  int height2dwidth = lua_tonumber(L, 2);
  int thickness2d = lua_tonumber(L, 3);
  int height3d = lua_tonumber(L, 3);
  int height3dwidth = lua_tonumber(L, 5);
  int thickness3d = lua_tonumber(L, 6);
  int return_code = set_scaledfont(height2d, height2dwidth, thickness2d,
                                   height3d, height3dwidth, thickness3d);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_get_fontsize(lua_State *L) {
  switch (fontindex) {
  case SMALL_FONT:
    lua_pushstring(L, "small");
    return 1;
    break;
  case LARGE_FONT:
    lua_pushstring(L, "large");
    return 1;
    break;
  case SCALED_FONT:
    lua_pushnumber(L, scaled_font2d_height);
    return 1;
    break;
  default:
    return luaL_error(L, "font size is invalid");
    break;
  }
}

int lua_set_scaledfont_height2d(lua_State *L) {
  int height2d = lua_tonumber(L, 1);
  int return_code = set_scaledfont_height2d(height2d);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showalltextures(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showalltextures(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showaxislabels(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showaxislabels(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showblocklabel(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showblocklabel(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showblocks(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showblocks(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showcadandgrid(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showcadandgrid(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showcadopaque(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showcadopaque(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showceiling(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showceiling(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showcolorbars(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showcolorbars(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showcvents(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 1);
  int return_code = set_showcvents(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showdummyvents(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showdummyvents(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showfloor(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showfloor(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showframe(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showframe(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showframelabel(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showframelabel(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showframerate(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showframerate(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showgrid(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showgrid(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showgridloc(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showgridloc(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showhmstimelabel(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showhmstimelabel(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showhrrcutoff(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showhrrcutoff(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showiso(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showiso(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showisonormals(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showisonormals(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showlabels(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showlabels(v);
  lua_pushnumber(L, return_code);
  return 1;
}

#ifdef pp_memstatus
int lua_set_showmemload(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showmemload(v);
  lua_pushnumber(L, return_code);
  return 1;
}
#endif

int lua_set_showopenvents(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 1);
  int return_code = set_showopenvents(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showothervents(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showothervents(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showsensors(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int return_code = set_showsensors(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showsliceinobst(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showsliceinobst(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showsmokepart(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showsmokepart(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showsprinkpart(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showsprinkpart(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showstreak(lua_State *L) {
  int show = lua_tonumber(L, 1);
  int step = lua_tonumber(L, 2);
  int showhead = lua_tonumber(L, 3);
  int index = lua_tonumber(L, 4);
  int return_code = set_showstreak(show, step, showhead, index);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showterrain(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showterrain(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showthreshold(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  float c = lua_tonumber(L, 3);
  int return_code = set_showthreshold(a, b, c);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showticks(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showticks(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtimebar(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtimebar(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtimelabel(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtimelabel(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtitle(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtitle(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtracersalways(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtracersalways(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtriangles(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int c = lua_tonumber(L, 3);
  int d = lua_tonumber(L, 4);
  int e = lua_tonumber(L, 5);
  int f = lua_tonumber(L, 6);
  int return_code = set_showtriangles(a, b, c, d, e, f);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtransparent(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtransparent(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtranparentvents(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtransparentvents(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtrianglecount(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtrianglecount(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showventflow(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int c = lua_tonumber(L, 3);
  int d = lua_tonumber(L, 4);
  int e = lua_tonumber(L, 5);
  int return_code = set_showventflow(a, b, c, d, e);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showvents(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showvents(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showwalls(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showwalls(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_skipembedslice(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_skipembedslice(v);
  lua_pushnumber(L, return_code);
  return 1;
}

#ifdef pp_SLICEUP
int lua_set_slicedup(lua_State *L) {
  int scalar = lua_tonumber(L, 1);
  int vector = lua_tonumber(L, 1);
  int return_code = set_slicedup(scalar, vector);
  lua_pushnumber(L, return_code);
  return 1;
}
#endif

int lua_set_smokesensors(lua_State *L) {
  int show = lua_tonumber(L, 1);
  int test = lua_tonumber(L, 2);
  int return_code = set_smokesensors(show, test);
  lua_pushnumber(L, return_code);
  return 1;
}

// int set_smoothblocksolid(int v); // SMOOTHBLOCKSOLID
#ifdef pp_LANG
int lua_set_startuplang(lua_State *L) {
  const char *lang = lua_tostring(L, 1);
  int return_code = set_startuplang(lang);
  lua_pushnumber(L, return_code);
  return 1;
}
#endif

int lua_set_stereo(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_stereo(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_surfinc(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_surfinc(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_terrainparams(lua_State *L) {
  int r_min = lua_tonumber(L, 1);
  int g_min = lua_tonumber(L, 2);
  int b_min = lua_tonumber(L, 3);
  int r_max = lua_tonumber(L, 4);
  int g_max = lua_tonumber(L, 5);
  int b_max = lua_tonumber(L, 6);
  int vert_factor = lua_tonumber(L, 7);
  int return_code =
      set_terrainparams(r_min, g_min, b_min, r_max, g_max, b_max, vert_factor);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_titlesafe(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_titlesafe(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_trainermode(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_trainermode(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_trainerview(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_trainerview(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_transparent(lua_State *L) {
  int use_flag = lua_tonumber(L, 1);
  float level = lua_tonumber(L, 2);
  int return_code = set_transparent(use_flag, level);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_treeparms(lua_State *L) {
  int minsize = lua_tonumber(L, 1);
  int visx = lua_tonumber(L, 2);
  int visy = lua_tonumber(L, 3);
  int visz = lua_tonumber(L, 4);
  int return_code = set_treeparms(minsize, visx, visy, visz);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_twosidedvents(lua_State *L) {
  int internal = lua_tonumber(L, 1);
  int external = lua_tonumber(L, 2);
  int return_code = set_twosidedvents(internal, external);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_vectorskip(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_vectorskip(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_volsmoke(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int c = lua_tonumber(L, 3);
  int d = lua_tonumber(L, 4);
  int e = lua_tonumber(L, 5);
  float f = lua_tonumber(L, 6);
  float g = lua_tonumber(L, 7);
  float h = lua_tonumber(L, 8);
  float i = lua_tonumber(L, 9);
  float j = lua_tonumber(L, 10);
  float k = lua_tonumber(L, 11);
  float l = lua_tonumber(L, 12);
  int return_code = set_volsmoke(a, b, c, d, e, f, g, h, i, j, k, l);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_zoom(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int return_code = set_zoom(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

// *** MISC ***
int lua_set_cellcentertext(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_cellcentertext(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_inputfile(lua_State *L) {
  const char *inputfile = lua_tostring(L, 1);
  int return_code = set_inputfile(inputfile);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_labelstartupview(lua_State *L) {
  const char *viewname = lua_tostring(L, 1);
  int return_code = set_labelstartupview(viewname);
  lua_pushnumber(L, return_code);
  return 1;
}

// DEPRECATED
// int lua_set_pixelskip(lua_State *L) {
//   int v = lua_tonumber(L, 1);
//   int return_code = set_pixelskip(v);
//   lua_pushnumber(L, return_code);
//   return 1;
// }

int lua_set_renderclip(lua_State *L) {
  int use_flag = lua_tonumber(L, 1);
  int left = lua_tonumber(L, 2);
  int right = lua_tonumber(L, 3);
  int bottom = lua_tonumber(L, 4);
  int top = lua_tonumber(L, 5);
  int return_code = set_renderclip(use_flag, left, right, bottom, top);
  lua_pushnumber(L, return_code);
  return 1;
}

// DEPRECATED
// int lua_set_renderfilelabel(lua_State *L) {
//   int v = lua_tonumber(L, 1);
//   int return_code = set_renderfilelabel(v);
//   lua_pushnumber(L, return_code);
//   return 1;
// }

int lua_set_renderfiletype(lua_State *L) {
  int render = lua_tonumber(L, 1);
  int movie = lua_tonumber(L, 2);
  int return_code = set_renderfiletype(render, movie);
  lua_pushnumber(L, return_code);
  return 1;
}

// int lua_set_skybox(lua_State *L){
//   return 0;
// }

// DEPRECATED
// int lua_set_renderoption(lua_State *L) {
//   int opt = lua_tonumber(L, 1);
//   int rows = lua_tonumber(L, 1);
//   int return_code = set_renderoption(opt, rows);
//   lua_pushnumber(L, return_code);
//   return 1;
// }

int lua_get_unit_defs(lua_State *L, f_units unitclass) {
  lua_createtable(L, 0, 4);
  // Loop through all of the units
  int j;
  for (j = 0; j < unitclass.nunits; j++) {
    lua_pushstring(L, unitclass.units[j].unit);
    lua_createtable(L, 0, 4);

    lua_pushstring(L, unitclass.units[j].unit);
    lua_setfield(L, -2, "unit");

    lua_pushstring(L, "scale");
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, unitclass.units[j].scale[0]);
    lua_setfield(L, -2, "factor");
    lua_pushnumber(L, unitclass.units[j].scale[1]);
    lua_setfield(L, -2, "offset");
    lua_settable(L, -3);

    lua_pushstring(L, unitclass.units[j].rel_val);
    lua_setfield(L, -2, "rel_val");

    lua_pushboolean(L, unitclass.units[j].rel_defined);
    lua_setfield(L, -2, "rel_defined");

    lua_settable(L, -3);
  }
  return 1;
}

// TODO: implement iterators for this table
int lua_get_unitclass(lua_State *L) {
  const char *classname = lua_tostring(L, 1);
  int i;
  for (i = 0; i < nunitclasses_default; i++) {
    // if the classname matches, put a table on the stack
    if (strcmp(classname, unitclasses_default[i].unitclass) == 0) {
      lua_createtable(L, 0, 4);
      // Loop through all of the units
      lua_get_unit_defs(L, unitclasses_default[i]);
      return 1;
    }
  }
  return 0;
}

int lua_get_units(lua_State *L) {
  const char *classname = lua_tostring(L, 1);
  int i;
  for (i = 0; i < nunitclasses_default; i++) {
    // if the classname matches, put a table on the stack
    if (strcmp(classname, unitclasses_default[i].unitclass) == 0) {
      // lua_createtable(L, 0, 4);
      // // Loop through all of the units
      // lua_get_units(L, unitclasses_default[i]);
      lua_pushstring(
          L,
          unitclasses_default[i].units[unitclasses_default[i].unit_index].unit);
      return 1;
    }
  }
  return 0;
}

int lua_set_units(lua_State *L) {
  const char *unitclassname = lua_tostring(L, 1);
  const char *unitname = lua_tostring(L, 2);

  size_t unitclass_index;
  bool unit_class_found = false;
  size_t unit_index;
  bool unit_index_found = false;
  for (size_t i = 0; i < nunitclasses_default; i++) {
    if (strcmp(unitclasses[i].unitclass, unitclassname) == 0) {
      unitclass_index = i;
      unit_class_found = true;
      break;
    }
  }
  if (!unit_class_found) {
    return luaL_error(L, "unit class index not found");
  }
  for (size_t i = 0; i < unitclasses[unitclass_index].nunits; i++) {
    if (strcmp(unitclasses[unitclass_index].units[i].unit, unitname) == 0) {
      unit_index = i;
      unit_index_found = true;
      break;
    }
  }
  if (!unit_index_found) {
    return luaL_error(L, "unit index not found");
  }
  set_units(unitclass_index, unit_index);
  return 0;
}

int lua_set_unitclasses(lua_State *L) {
  int i = 0;
  int n = 0;
  if (!lua_istable(L, -1)) {
    fprintf(stderr, "stack is not a table at index\n");
    exit(1);
  }
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    n++;
  }
  if (n > 0) {
    int *indices = malloc(sizeof(int) * n);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      indices[i] = lua_tonumber(L, -1);
      lua_pop(L, 1);
      i++;
    }
    int return_code = set_unitclasses(n, indices);
    lua_pushnumber(L, return_code);
    free(indices);
    return 1;
  } else {
    return 0;
  }
}

int lua_set_zaxisangles(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int c = lua_tonumber(L, 3);
  int return_code = set_zaxisangles(a, b, c);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_colorbartype(lua_State *L) {
  int type = lua_tonumber(L, 1);
  const char *label = lua_tostring(L, 2);
  int return_code = set_colorbartype(type, label);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_extremecolors(lua_State *L) {
  int rmin = lua_tonumber(L, 1);
  int gmin = lua_tonumber(L, 2);
  int bmin = lua_tonumber(L, 3);
  int rmax = lua_tonumber(L, 4);
  int gmax = lua_tonumber(L, 5);
  int bmax = lua_tonumber(L, 6);
  int return_code = set_extremecolors(rmin, gmin, bmin, rmax, gmax, bmax);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_firecolor(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_firecolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_firecolormap(lua_State *L) {
  int type = lua_tonumber(L, 1);
  int index = lua_tonumber(L, 2);
  int return_code = set_firecolormap(type, index);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_firedepth(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_firedepth(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showextremedata(lua_State *L) {
  int show_extremedata = lua_tonumber(L, 1);
  int below = lua_tonumber(L, 2);
  int above = lua_tonumber(L, 3);
  int return_code = set_showextremedata(show_extremedata, below, above);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smokecolor(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_smokecolor(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smokecull(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_smokecull(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smokeskip(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_smokeskip(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_smokealbedo(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_smokealbedo(v);
  lua_pushnumber(L, return_code);
  return 1;
}

#ifdef pp_GPU
int lua_set_smokerthick(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_smokerthick(v);
  lua_pushnumber(L, return_code);
  return 1;
}
#endif

#ifdef pp_GPU
int lua_set_usegpu(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_usegpu(v);
  lua_pushnumber(L, return_code);
  return 1;
}
#endif

// *** ZONE FIRE PARAMETRES ***
int lua_set_showhazardcolors(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showhazardcolors(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showhzone(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showhzone(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showszone(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showszone(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showvzone(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showvzone(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showzonefire(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showzonefire(v);
  lua_pushnumber(L, return_code);
  return 1;
}

// *** TOUR INFO ***
int lua_set_showpathnodes(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showpathnodes(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_showtourroute(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showtourroute(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_tourcolors_selectedpathline(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_selectedpathline(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}
int lua_set_tourcolors_selectedpathlineknots(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_selectedpathlineknots(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}
int lua_set_tourcolors_selectedknot(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_selectedknot(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}
int lua_set_tourcolors_pathline(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_selectedpathline(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}
int lua_set_tourcolors_pathknots(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_pathknots(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}
int lua_set_tourcolors_text(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_text(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}
int lua_set_tourcolors_avatar(lua_State *L) {
  int r = lua_tonumber(L, 1);
  int g = lua_tonumber(L, 2);
  int b = lua_tonumber(L, 3);
  int return_code = set_tourcolors_avatar(r, g, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_viewalltours(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_viewalltours(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_viewtimes(lua_State *L) {
  float start = lua_tonumber(L, 1);
  float stop = lua_tonumber(L, 2);
  int ntimes = lua_tonumber(L, 3);
  int return_code = set_viewtimes(start, stop, ntimes);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_viewtourfrompath(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_viewtourfrompath(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_devicevectordimensions(lua_State *L) {
  float baselength = lua_tonumber(L, 1);
  float basediameter = lua_tonumber(L, 2);
  float headlength = lua_tonumber(L, 3);
  float headdiameter = lua_tonumber(L, 4);
  int return_code = set_devicevectordimensions(baselength, basediameter,
                                               headlength, headdiameter);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_devicebounds(lua_State *L) {
  float min = lua_tonumber(L, 1);
  float max = lua_tonumber(L, 2);
  int return_code = set_devicebounds(min, max);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_deviceorientation(lua_State *L) {
  int a = lua_tonumber(L, 1);
  float b = lua_tonumber(L, 2);
  int return_code = set_deviceorientation(a, b);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_gridparms(lua_State *L) {
  int vx = lua_tonumber(L, 1);
  int vy = lua_tonumber(L, 2);
  int vz = lua_tonumber(L, 3);
  int px = lua_tonumber(L, 4);
  int py = lua_tonumber(L, 5);
  int pz = lua_tonumber(L, 6);
  int return_code = set_gridparms(vx, vy, vz, px, py, pz);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_gsliceparms(lua_State *L) {
  int i;
  int vis_data = lua_tonumber(L, 1);
  int vis_triangles = lua_tonumber(L, 2);
  int vis_triangulation = lua_tonumber(L, 3);
  int vis_normal = lua_tonumber(L, 4);
  float xyz[3];
  // TODO: use named fields (e.g. xyz)
  for (i = 0; i < 3; i++) {
    lua_pushnumber(L, i);
    lua_gettable(L, 5);
    xyz[i] = lua_tonumber(L, -1);
    lua_pop(L, 1);
    i++;
  }
  float azelev[2];
  for (i = 0; i < 2; i++) {
    lua_pushnumber(L, i);
    lua_gettable(L, 6);
    azelev[i] = lua_tonumber(L, -1);
    lua_pop(L, 1);
    i++;
  }
  int return_code = set_gsliceparms(vis_data, vis_triangles, vis_triangulation,
                                    vis_normal, xyz, azelev);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_loadfilesatstartup(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_loadfilesatstartup(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_mscale(lua_State *L) {
  float a = lua_tonumber(L, 1);
  float b = lua_tonumber(L, 2);
  float c = lua_tonumber(L, 3);
  int return_code = set_mscale(a, b, c);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_sliceauto(lua_State *L) {
  lua_pushnil(L);
  int n = 0;
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    n++;
  }
  if (n > 0) {
    int i = 0;
    int *vals = malloc(sizeof(int) * n);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      vals[i] = lua_tonumber(L, -1);
      lua_pop(L, 1);
      i++;
    }
    int return_code = set_sliceauto(n, vals);
    lua_pushnumber(L, return_code);
    free(vals);
    return 1;
  } else {
    return 0;
  }
}

int lua_set_msliceauto(lua_State *L) {
  lua_pushnil(L);
  int n = 0;
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    n++;
  }
  if (n > 0) {
    int i = 0;
    int *vals = malloc(sizeof(int) * n);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      vals[i] = lua_tonumber(L, -1);
      lua_pop(L, 1);
      i++;
    }
    int return_code = set_msliceauto(n, vals);
    lua_pushnumber(L, return_code);
    free(vals);
    return 1;
  } else {
    return 0;
  }
}

int lua_set_compressauto(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_compressauto(v);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_propindex(lua_State *L) {
  lua_pushnil(L);
  int n = 0;
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    n++;
  }
  if (n > 0) {
    int i = 0;
    int *vals = malloc(sizeof(int) * n * PROPINDEX_STRIDE);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      lua_pushnumber(L, 1);
      lua_gettable(L, -2);
      *(vals + (i * PROPINDEX_STRIDE + 0)) = lua_tonumber(L, -1);
      lua_pop(L, 1);

      lua_pushnumber(L, 1);
      lua_gettable(L, -2);
      *(vals + (i * PROPINDEX_STRIDE + 1)) = lua_tonumber(L, -1);
      lua_pop(L, 1);

      lua_pop(L, 1);
      i++;
    }
    int return_code = set_propindex(n, vals);
    lua_pushnumber(L, return_code);
    return 1;
  } else {
    return 0;
  }
}

int lua_set_showdevices(lua_State *L) {
  lua_pushnil(L);
  int n = 0;
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    n++;
  }
  if (n > 0) {
    int i = 0;
    const char **names = malloc(sizeof(char *) * n);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      names[i] = lua_tostring(L, -1);
      lua_pop(L, 1);
      i++;
    }
    int return_code = set_showdevices(n, names);
    lua_pushnumber(L, return_code);
    free(names);
    return 1;
  } else {
    return 0;
  }
} // SHOWDEVICES

int lua_set_showdevicevals(lua_State *L) {
  int a = lua_tonumber(L, 1);
  int b = lua_tonumber(L, 2);
  int c = lua_tonumber(L, 3);
  int d = lua_tonumber(L, 4);
  int e = lua_tonumber(L, 5);
  int f = lua_tonumber(L, 6);
  int g = lua_tonumber(L, 7);
  int h = lua_tonumber(L, 8);
  int return_code = set_showdevicevals(a, b, c, d, e, f, g, h);
  lua_pushnumber(L, return_code);
  return 1;
} // SHOWDEVICEVALS

int lua_set_showmissingobjects(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_showmissingobjects(v);
  lua_pushnumber(L, return_code);
  return 1;
} // SHOWMISSINGOBJECTS

int lua_set_tourindex(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_tourindex(v);
  lua_pushnumber(L, return_code);
  return 1;
} // TOURINDEX

int lua_set_c_particles(lua_State *L) {
  int minFlag = lua_tonumber(L, 1);
  float minValue = lua_tonumber(L, 2);
  int maxFlag = lua_tonumber(L, 3);
  float maxValue = lua_tonumber(L, 4);
  const char *label = NULL;
  if (lua_gettop(L) == 5) {
    label = lua_tostring(L, 5);
  }
  int return_code =
      set_c_particles(minFlag, minValue, maxFlag, maxValue, label);
  lua_pushnumber(L, return_code);
  return 1;
} // C_PARTICLES

int lua_set_c_slice(lua_State *L) {
  int minFlag = lua_tonumber(L, 1);
  float minValue = lua_tonumber(L, 2);
  int maxFlag = lua_tonumber(L, 3);
  float maxValue = lua_tonumber(L, 4);
  const char *label = NULL;
  if (lua_gettop(L) == 5) {
    label = lua_tostring(L, 5);
  }
  int return_code = set_c_slice(minFlag, minValue, maxFlag, maxValue, label);
  lua_pushnumber(L, return_code);
  return 1;
} // C_SLICE

int lua_set_cache_boundarydata(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_cache_boundarydata(v);
  lua_pushnumber(L, return_code);
  return 1;
} // CACHE_BOUNDARYDATA

int lua_set_cache_qdata(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_cache_qdata(v);
  lua_pushnumber(L, return_code);
  return 1;
} // CACHE_QDATA

int lua_set_percentilelevel(lua_State *L) {
  float p_level_min = lua_tonumber(L, 1);
  float p_level_max = lua_tonumber(L, 2);
  int return_code = set_percentilelevel(p_level_min, p_level_max);
  lua_pushnumber(L, return_code);
  return 1;
} // PERCENTILELEVEL

int lua_set_timeoffset(lua_State *L) {
  int v = lua_tonumber(L, 1);
  int return_code = set_timeoffset(v);
  lua_pushnumber(L, return_code);
  return 1;
} // TIMEOFFSET

int lua_set_tload(lua_State *L) {
  int beginFlag = lua_tonumber(L, 1);
  float beginVal = lua_tonumber(L, 2);
  int endFlag = lua_tonumber(L, 3);
  float endVal = lua_tonumber(L, 4);
  int skipFlag = lua_tonumber(L, 5);
  float skipVal = lua_tonumber(L, 6);
  int return_code =
      set_tload(beginFlag, beginVal, endFlag, endVal, skipFlag, skipVal);
  lua_pushnumber(L, return_code);
  return 1;
} // TLOAD

int lua_set_v_slice(lua_State *L) {
  int minFlag = lua_tonumber(L, 1);
  float minValue = lua_tonumber(L, 2);
  int maxFlag = lua_tonumber(L, 3);
  float maxValue = lua_tonumber(L, 4);
  const char *label = lua_tostring(L, 5);
  float lineMin = lua_tonumber(L, 6);
  float lineMax = lua_tonumber(L, 7);
  int lineNum = lua_tonumber(L, 8);
  int return_code = set_v_slice(minFlag, minValue, maxFlag, maxValue, label,
                                lineMin, lineMax, lineNum);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_set_patchdataout(lua_State *L) {
  int outputFlag = lua_tonumber(L, 1);
  int tmin = lua_tonumber(L, 1);
  int tmax = lua_tonumber(L, 2);
  int xmin = lua_tonumber(L, 3);
  int xmax = lua_tonumber(L, 4);
  int ymin = lua_tonumber(L, 5);
  int ymax = lua_tonumber(L, 6);
  int zmin = lua_tonumber(L, 7);
  int zmax = lua_tonumber(L, 8);
  int return_code = set_patchdataout(outputFlag, tmin, tmax, xmin, xmax, ymin,
                                     ymax, zmin, zmax);
  lua_pushnumber(L, return_code);
  return 1;
} // PATCHDATAOUT

int lua_show_smoke3d_showall(lua_State *L) {
  int return_code = show_smoke3d_showall();
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_show_smoke3d_hideall(lua_State *L) {
  int return_code = show_smoke3d_hideall();
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_show_slices_showall(lua_State *L) {
  int return_code = show_slices_showall();
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_show_slices_hideall(lua_State *L) {
  int return_code = show_slices_hideall();
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_add_title_line(lua_State *L) {
  const char *string = lua_tostring(L, 1);
  int return_code = addTitleLine(&titleinfo, string);
  lua_pushnumber(L, return_code);
  return 1;
}

int lua_clear_title_lines(lua_State *L) {
  int return_code = clearTitleLines(&titleinfo);
  lua_pushnumber(L, return_code);
  return 1;
}

/// @brief Add paths for the Lua interpreter to find scripts and libraries that
/// are written in Lua.
/// @param L The Lua interpreter state
void addScriptPath(lua_State *L) {
  // package.path is a path variable where Lua scripts and modules may be
  // found, typically text based files with the .lua extension.
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");
  const char *original_path = lua_tostring(L, -1);
  int new_length = strlen(original_path) + 1;
#ifdef pp_LINUX
  // Add script path for the linux install
  char *linux_share_path = ";/usr/share/smokeview/?.lua";
  new_length += strlen(linux_share_path);
#endif
  // Add the location of the smokeview binary as a place for scripts. This is
  // mostly useful for running tests.
  char *bin_path = malloc(sizeof(char) * (strlen(smokeview_bindir_abs) + 8));
  sprintf(bin_path, ";%s/?.lua", smokeview_bindir_abs);
  new_length += strlen(bin_path);
  // Create the path.
  char *new_path = malloc(sizeof(char) * new_length);
  strcpy(new_path, original_path);
#ifdef pp_LINUX
  strcat(new_path, linux_share_path);
#endif
  strcat(new_path, bin_path);
  lua_pushstring(L, new_path);
  lua_setfield(L, -3, "path");
  lua_pop(L, 1); // pop the now redundant "path" variable from the stack
  lua_pop(L, 1); // pop the now redundant "package" variable from the stack
  free(new_path);
  free(bin_path);
}

/// @brief Add paths for the Lua interpreter to find libraries that are compiled
/// to shared libraries.
/// @param L The Lua interpreter state
void addCPath(lua_State *L) {
  // package.path is a path variable where Lua scripts and modules may be
  // found, typically text based files with the .lua extension.
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "cpath");
  const char *original_path = lua_tostring(L, -1);
  int new_length = strlen(original_path) + 1;
#ifdef pp_LINUX
  char *so_extension = ".so";
#else
  char *so_extension = ".dll";
#endif
  // Add the location of the smokeview binary as a place for scripts. This is
  // mostly useful for running tests.
  char *bin_path = malloc(sizeof(char) * (strlen(smokeview_bindir_abs) + 8));
  sprintf(bin_path, ";%s/?%s", smokeview_bindir_abs, so_extension);
  new_length += strlen(bin_path);
  // Create the path.
  char *new_path = malloc(sizeof(char) * new_length);
  strcpy(new_path, original_path);
  strcat(new_path, bin_path);
  lua_pushstring(L, new_path);
  lua_setfield(L, -3, "cpath");
  lua_pop(L, 1); // pop the now redundant "path" variable from the stack
  lua_pop(L, 1); // pop the now redundant "package" variable from the stack
  free(new_path);
  free(bin_path);
}

/// @brief Add paths for the Lua interpreter to find scripts and libraries.
/// @param L The Lua interpreter state
void addLuaPaths(lua_State *L) {
  // Add the paths for *.lua files.
  addScriptPath(L);
  // Ad the path for native (*.dll, and *.so) libs
  addCPath(L);
  return;
}

static luaL_Reg const smvlib[] = {
    {"set_slice_bounds", lua_set_slice_bounds},
    {"set_slice_bound_min", lua_set_slice_bound_min},
    {"set_slice_bound_max", lua_set_slice_bound_max},
    {"get_slice_bounds", lua_get_slice_bounds},
    {"loadsmvall", lua_loadsmvall},
    {"hidewindow", lua_hidewindow},
    {"yieldscript", lua_yieldscript},
    {"tempyieldscript", lua_tempyieldscript},
    {"displayCB", lua_displayCB},
    {"renderclip", lua_renderclip},
    {"renderC", lua_render},
    {"render_var", lua_render_var},
    {"gsliceview", lua_gsliceview},
    {"showplot3ddata", lua_showplot3ddata},
    {"gslicepos", lua_gslicepos},
    {"gsliceorien", lua_gsliceorien},
    {"settourkeyframe", lua_settourkeyframe},
    {"settourview", lua_settourview},
    {"getframe", lua_getframe},
    {"setframe", lua_setframe},
    {"gettime", lua_gettime},
    {"settime", lua_settime},
    {"loaddatafile", lua_loaddatafile},
    {"loadinifile", lua_loadinifile},
    {"loadvdatafile", lua_loadvdatafile},
    {"loadboundaryfile", lua_loadboundaryfile},
    {"load3dsmoke", lua_load3dsmoke},
    {"loadvolsmoke", lua_loadvolsmoke},
    {"loadvolsmokeframe", lua_loadvolsmokeframe},
    {"set_rendertype", lua_set_rendertype},
    {"get_rendertype", lua_get_rendertype},
    {"set_movietype", lua_set_movietype},
    {"get_movietype", lua_get_movietype},
    {"makemovie", lua_makemovie},
    {"loadtour", lua_loadtour},
    {"loadparticles", lua_loadparticles},
    {"partclasscolor", lua_partclasscolor},
    {"partclasstype", lua_partclasstype},
    {"plot3dprops", lua_plot3dprops},
    {"loadplot3d", lua_loadplot3d},
    {"loadslice", lua_loadslice},
    {"loadsliceindex", lua_loadsliceindex},
    {"loadvslice", lua_loadvslice},
    {"loadiso", lua_loadiso},
    {"unloadall", lua_unloadall},
    {"unloadtour", lua_unloadtour},
    {"setrenderdir", lua_setrenderdir},
    {"getrenderdir", lua_getrenderdir},
    {"set_ortho_preset", lua_set_ortho_preset},
    {"setviewpoint", lua_setviewpoint},
    {"getviewpoint", lua_getviewpoint},
    {"exit", lua_exit_smokeview},
    {"getcolorbarflip", lua_getcolorbarflip},
    {"setcolorbarflip", lua_setcolorbarflip},
    {"setwindowsize", lua_setwindowsize},
    // {"window.setwindowsize", lua_setwindowsize},
    {"setgridvisibility", lua_setgridvisibility},
    {"setgridparms", lua_setgridparms},
    {"setcolorbarindex", lua_setcolorbarindex},
    {"getcolorbarindex", lua_getcolorbarindex},

    {"set_slice_in_obst", lua_set_slice_in_obst},
    {"get_slice_in_obst", lua_get_slice_in_obst},

    // colorbar
    {"set_colorbar", lua_set_colorbar},
    {"set_named_colorbar", lua_set_named_colorbar},
    // {"get_named_colorbar", lua_get_named_colorbar},

    {"set_colorbar_visibility", lua_set_colorbar_visibility},
    {"get_colorbar_visibility", lua_get_colorbar_visibility},
    {"toggle_colorbar_visibility", lua_toggle_colorbar_visibility},

    {"set_colorbar_visibility_horizontal",
     lua_set_colorbar_visibility_horizontal},
    {"get_colorbar_visibility_horizontal",
     lua_get_colorbar_visibility_horizontal},
    {"toggle_colorbar_visibility_horizontal",
     lua_toggle_colorbar_visibility_horizontal},

    {"set_colorbar_visibility_vertical", lua_set_colorbar_visibility_vertical},
    {"get_colorbar_visibility_vertical", lua_get_colorbar_visibility_vertical},
    {"toggle_colorbar_visibility_vertical",
     lua_toggle_colorbar_visibility_vertical},

    // timebar
    {"set_timebar_visibility", lua_set_timebar_visibility},
    {"get_timebar_visibility", lua_get_timebar_visibility},
    {"toggle_timebar_visibility", lua_toggle_timebar_visibility},

    // title
    {"set_title_visibility", lua_set_title_visibility},
    {"get_title_visibility", lua_get_title_visibility},
    {"toggle_title_visibility", lua_toggle_title_visibility},

    // smv_version
    {"set_smv_version_visibility", lua_set_smv_version_visibility},
    {"get_smv_version_visibility", lua_get_smv_version_visibility},
    {"toggle_smv_version_visibility", lua_toggle_smv_version_visibility},

    // chid
    {"set_chid_visibility", lua_set_chid_visibility},
    {"get_chid_visibility", lua_get_chid_visibility},
    {"toggle_chid_visibility", lua_toggle_chid_visibility},

    // blockages
    {"blockages_hide_all", lua_blockages_hide_all},
    // {"get_chid_visibility", lua_get_chid_visibility},
    // {"toggle_chid_visibility", lua_toggle_chid_visibility},

    // outlines
    {"outlines_show", lua_outlines_show},
    {"outlines_hide", lua_outlines_hide},

    // surfaces
    {"surfaces_hide_all", lua_surfaces_hide_all},
    // devices
    {"devices_hide_all", lua_devices_hide_all},

    // axis
    {"set_axis_visibility", lua_set_axis_visibility},
    {"get_axis_visibility", lua_get_axis_visibility},
    {"toggle_axis_visibility", lua_toggle_axis_visibility},

    // frame label
    {"set_framelabel_visibility", lua_set_framelabel_visibility},
    {"get_framelabel_visibility", lua_get_framelabel_visibility},
    {"toggle_framelabel_visibility", lua_toggle_framelabel_visibility},

    // framerate
    {"set_framerate_visibility", lua_set_framerate_visibility},
    {"get_framerate_visibility", lua_get_framerate_visibility},
    {"toggle_framerate_visibility", lua_toggle_framerate_visibility},

    // grid locations
    {"set_gridloc_visibility", lua_set_gridloc_visibility},
    {"get_gridloc_visibility", lua_get_gridloc_visibility},
    {"toggle_gridloc_visibility", lua_toggle_gridloc_visibility},

    // hrrpuv cutoff
    {"set_hrrcutoff_visibility", lua_set_hrrcutoff_visibility},
    {"get_hrrcutoff_visibility", lua_get_hrrcutoff_visibility},
    {"toggle_hrrcutoff_visibility", lua_toggle_hrrcutoff_visibility},

    // hrr label
    {"set_hrrlabel_visibility", lua_set_hrrlabel_visibility},
    {"get_hrrlabel_visibility", lua_get_hrrlabel_visibility},
    {"toggle_hrrlabel_visibility", lua_toggle_hrrlabel_visibility},

// memory load
#ifdef pp_memstatus
    {"set_memload_visibility", lua_set_memload_visibility},
    {"get_memload_visibility", lua_get_memload_visibility},
    {"toggle_memload_visibility", lua_toggle_memload_visibility},
#endif

    // mesh label
    {"set_meshlabel_visibility", lua_set_meshlabel_visibility},
    {"get_meshlabel_visibility", lua_get_meshlabel_visibility},
    {"toggle_meshlabel_visibility", lua_toggle_meshlabel_visibility},

    // slice average
    {"set_slice_average_visibility", lua_set_slice_average_visibility},
    {"get_slice_average_visibility", lua_get_slice_average_visibility},
    {"toggle_slice_average_visibility", lua_toggle_slice_average_visibility},

    // time
    {"set_time_visibility", lua_set_time_visibility},
    {"get_time_visibility", lua_get_time_visibility},
    {"toggle_time_visibility", lua_toggle_time_visibility},

    // user settable ticks
    {"set_user_ticks_visibility", lua_set_user_ticks_visibility},
    {"get_user_ticks_visibility", lua_get_user_ticks_visibility},
    {"toggle_user_ticks_visibility", lua_toggle_user_ticks_visibility},

    // version info
    {"set_version_info_visibility", lua_set_version_info_visibility},
    {"get_version_info_visibility", lua_get_version_info_visibility},
    {"toggle_version_info_visibility", lua_toggle_version_info_visibility},

    // set all
    {"set_all_label_visibility", lua_set_all_label_visibility},

    // set the blockage view method
    {"blockage_view_method", lua_blockage_view_method},
    {"blockage_outline_color", lua_blockage_outline_color},
    {"blockage_locations", lua_blockage_locations},

    {"set_colorbar_colors", lua_set_colorbar_colors},
    {"get_colorbar_colors", lua_get_colorbar_colors},
    {"set_color2bar_colors", lua_set_color2bar_colors},
    {"get_color2bar_colors", lua_get_color2bar_colors},

    // Camera API
    {"camera_mod_eyex", lua_camera_mod_eyex},
    {"camera_set_eyex", lua_camera_set_eyex},
    {"camera_get_eyex", lua_camera_get_eyex},

    {"camera_mod_eyey", lua_camera_mod_eyey},
    {"camera_set_eyey", lua_camera_set_eyey},
    {"camera_get_eyey", lua_camera_get_eyey},

    {"camera_mod_eyez", lua_camera_mod_eyez},
    {"camera_set_eyez", lua_camera_set_eyez},
    {"camera_get_eyez", lua_camera_get_eyez},

    {"camera_mod_az", lua_camera_mod_az},
    {"camera_set_az", lua_camera_set_az},
    {"camera_get_az", lua_camera_get_az},
    {"camera_mod_elev", lua_camera_mod_elev},
    {"camera_zoom_to_fit", lua_camera_zoom_to_fit},
    {"camera_set_elev", lua_camera_set_elev},
    {"camera_get_elev", lua_camera_get_elev},

    {"camera_set_viewdir", lua_camera_set_viewdir},
    {"camera_get_viewdir", lua_camera_get_viewdir},

    {"camera_get_zoom", lua_camera_get_zoom},
    {"camera_set_zoom", lua_camera_set_zoom},

    {"camera_get_rotation_type", lua_camera_get_rotation_type},
    {"camera_get_rotation_index", lua_camera_get_rotation_index},
    {"camera_set_rotation_type", lua_camera_set_rotation_type},
    {"camera_get_projection_type", lua_camera_get_projection_type},
    {"camera_set_projection_type", lua_camera_set_projection_type},

    {"get_clipping_mode", lua_get_clipping_mode},
    {"set_clipping_mode", lua_set_clipping_mode},
    {"set_sceneclip_x", lua_set_sceneclip_x},
    {"set_sceneclip_x_min", lua_set_sceneclip_x_min},
    {"set_sceneclip_x_max", lua_set_sceneclip_x_max},
    {"set_sceneclip_y", lua_set_sceneclip_y},
    {"set_sceneclip_y_min", lua_set_sceneclip_y_min},
    {"set_sceneclip_y_max", lua_set_sceneclip_y_max},
    {"set_sceneclip_z", lua_set_sceneclip_z},
    {"set_sceneclip_z_min", lua_set_sceneclip_z_min},
    {"set_sceneclip_z_max", lua_set_sceneclip_z_max},

    {"set_ambientlight", lua_set_ambientlight},
    {"get_backgroundcolor", lua_get_backgroundcolor},
    {"set_backgroundcolor", lua_set_backgroundcolor},
    {"set_blockcolor", lua_set_blockcolor},
    {"set_blockshininess", lua_set_blockshininess},
    {"set_blockspecular", lua_set_blockspecular},
    {"set_boundcolor", lua_set_boundcolor},
    {"set_diffuselight", lua_set_diffuselight},
    {"set_directioncolor", lua_set_directioncolor},
    {"get_flip", lua_get_flip},
    {"set_flip", lua_set_flip},
    {"get_foregroundcolor", lua_get_foregroundcolor},
    {"set_foregroundcolor", lua_set_foregroundcolor},
    {"set_heatoffcolor", lua_set_heatoffcolor},
    {"set_heatoncolor", lua_set_heatoncolor},
    {"set_isocolors", lua_set_isocolors},
    {"set_colortable", lua_set_colortable},
    {"set_lightpos0", lua_set_lightpos0},
    {"set_lightpos1", lua_set_lightpos1},
    {"set_sensorcolor", lua_set_sensorcolor},
    {"set_sensornormcolor", lua_set_sensornormcolor},
    {"set_bw", lua_set_bw},
    {"set_sprinkleroffcolor", lua_set_sprinkleroffcolor},
    {"set_sprinkleroncolor", lua_set_sprinkleroncolor},
    {"set_staticpartcolor", lua_set_staticpartcolor},
    {"set_timebarcolor", lua_set_timebarcolor},
    {"set_ventcolor", lua_set_ventcolor},
    {"set_gridlinewidth", lua_set_gridlinewidth},
    {"set_isolinewidth", lua_set_isolinewidth},
    {"set_isopointsize", lua_set_isopointsize},
    {"set_linewidth", lua_set_linewidth},
    {"set_partpointsize", lua_set_partpointsize},
    {"set_plot3dlinewidth", lua_set_plot3dlinewidth},
    {"set_plot3dpointsize", lua_set_plot3dpointsize},
    {"set_sensorabssize", lua_set_sensorabssize},
    {"set_sensorrelsize", lua_set_sensorrelsize},
    {"set_sliceoffset", lua_set_sliceoffset},
    {"set_smoothlines", lua_set_smoothlines},
    {"set_spheresegs", lua_set_spheresegs},
    {"set_sprinklerabssize", lua_set_sprinklerabssize},
    {"set_streaklinewidth", lua_set_streaklinewidth},
    {"set_ticklinewidth", lua_set_ticklinewidth},
    {"set_usenewdrawface", lua_set_usenewdrawface},
    {"set_veclength", lua_set_veclength},
    {"set_vectorlinewidth", lua_set_vectorlinewidth},
    {"set_vectorpointsize", lua_set_vectorpointsize},
    {"set_ventlinewidth", lua_set_ventlinewidth},
    {"set_ventoffset", lua_set_ventoffset},
    {"set_windowoffset", lua_set_windowoffset},
    {"set_windowwidth", lua_set_windowwidth},
    {"set_windowheight", lua_set_windowheight},

    {"set_boundzipstep", lua_set_boundzipstep},
    {"set_fed", lua_set_fed},
    {"set_fedcolorbar", lua_set_fedcolorbar},
    {"set_isozipstep", lua_set_isozipstep},
    {"set_nopart", lua_set_nopart},
    {"set_showfedarea", lua_set_showfedarea},
    {"set_sliceaverage", lua_set_sliceaverage},
    {"set_slicedataout", lua_set_slicedataout},
    {"set_slicezipstep", lua_set_slicezipstep},
    {"set_smoke3dzipstep", lua_set_smoke3dzipstep},
    {"set_userrotate", lua_set_userrotate},

    {"set_aperture", lua_set_aperture},
    // { "set_axissmooth", lua_set_axissmooth },
    {"set_blocklocation", lua_set_blocklocation},
    {"set_boundarytwoside", lua_set_boundarytwoside},
    {"set_clip", lua_set_clip},
    {"set_contourtype", lua_set_contourtype},
    {"set_cullfaces", lua_set_cullfaces},
    {"set_texturelighting", lua_set_texturelighting},
    {"set_eyeview", lua_set_eyeview},
    {"set_eyex", lua_set_eyex},
    {"set_eyey", lua_set_eyey},
    {"set_eyez", lua_set_eyez},
    {"get_fontsize", lua_get_fontsize},
    {"set_fontsize", lua_set_fontsize},
    {"set_frameratevalue", lua_set_frameratevalue},
    {"set_showfaces_solid", lua_set_showfaces_solid},
    {"set_showfaces_outline", lua_set_showfaces_outline},
    {"set_smoothgeomnormal", lua_set_smoothgeomnormal},
    {"set_geomvertexag", lua_set_geomvertexag},
    {"set_gversion", lua_set_gversion},
    {"set_isotran2", lua_set_isotran2},
    {"set_meshvis", lua_set_meshvis},
    {"set_meshoffset", lua_set_meshoffset},

    {"set_northangle", lua_set_northangle},
    {"set_offsetslice", lua_set_offsetslice},
    {"set_outlinemode", lua_set_outlinemode},
    {"set_p3dsurfacetype", lua_set_p3dsurfacetype},
    {"set_p3dsurfacesmooth", lua_set_p3dsurfacesmooth},
    {"set_scaledfont", lua_set_scaledfont},
    {"set_scaledfont_height2d", lua_set_scaledfont_height2d},
    {"set_showalltextures", lua_set_showalltextures},
    {"set_showaxislabels", lua_set_showaxislabels},
    {"set_showblocklabel", lua_set_showblocklabel},
    {"set_showblocks", lua_set_showblocks},
    {"set_showcadandgrid", lua_set_showcadandgrid},
    {"set_showcadopaque", lua_set_showcadopaque},
    {"set_showceiling", lua_set_showceiling},
    {"set_showcolorbars", lua_set_showcolorbars},
    {"set_showcvents", lua_set_showcvents},
    {"set_showdummyvents", lua_set_showdummyvents},
    {"set_showfloor", lua_set_showfloor},
    {"set_showframe", lua_set_showframe},
    {"set_showframelabel", lua_set_showframelabel},
    {"set_showframerate", lua_set_showframerate},
    {"set_showgrid", lua_set_showgrid},
    {"set_showgridloc", lua_set_showgridloc},
    {"set_showhmstimelabel", lua_set_showhmstimelabel},
    {"set_showhrrcutoff", lua_set_showhrrcutoff},
    {"set_showiso", lua_set_showiso},
    {"set_showisonormals", lua_set_showisonormals},
    {"set_showlabels", lua_set_showlabels},
#ifdef pp_memstatus
    {"set_showmemload", lua_set_showmemload},
#endif
    {"set_showopenvents", lua_set_showopenvents},
    {"set_showothervents", lua_set_showothervents},
    {"set_showsensors", lua_set_showsensors},
    {"set_showsliceinobst", lua_set_showsliceinobst},
    {"set_showsmokepart", lua_set_showsmokepart},
    {"set_showsprinkpart", lua_set_showsprinkpart},
    {"set_showstreak", lua_set_showstreak},
    {"set_showterrain", lua_set_showterrain},
    {"set_showthreshold", lua_set_showthreshold},
    {"set_showticks", lua_set_showticks},
    {"set_showtimebar", lua_set_showtimebar},
    {"set_showtimelabel", lua_set_showtimelabel},
    {"set_showtitle", lua_set_showtitle},
    {"set_showtracersalways", lua_set_showtracersalways},
    {"set_showtriangles", lua_set_showtriangles},
    {"set_showtransparent", lua_set_showtransparent},
    {"set_showtransparentvents", lua_set_showtranparentvents},
    {"set_showtrianglecount", lua_set_showtrianglecount},
    {"set_showventflow", lua_set_showventflow},
    {"set_showvents", lua_set_showvents},
    {"set_showwalls", lua_set_showwalls},
    {"set_skipembedslice", lua_set_skipembedslice},
#ifdef pp_SLICEUP
    {"set_slicedup", lua_set_slicedup},
#endif
    {"set_smokesensors", lua_set_smokesensors},
#ifdef pp_LANG
    {"set_startuplang", lua_set_startuplang},
#endif
    {"set_stereo", lua_set_stereo},
    {"set_surfinc", lua_set_surfinc},
    {"set_terrainparams", lua_set_terrainparams},
    {"set_titlesafe", lua_set_titlesafe},
    {"set_trainermode", lua_set_trainermode},
    {"set_trainerview", lua_set_trainerview},
    {"set_transparent", lua_set_transparent},
    {"set_treeparms", lua_set_treeparms},
    {"set_twosidedvents", lua_set_twosidedvents},
    {"set_vectorskip", lua_set_vectorskip},
    {"set_volsmoke", lua_set_volsmoke},
    {"set_zoom", lua_set_zoom},
    {"set_cellcentertext", lua_set_cellcentertext},
    {"set_inputfile", lua_set_inputfile},
    {"set_labelstartupview", lua_set_labelstartupview},
    // { "set_pixelskip", lua_set_pixelskip },
    {"set_renderclip", lua_set_renderclip},
    // { "set_renderfilelabel", lua_set_renderfilelabel },
    {"set_renderfiletype", lua_set_renderfiletype},

    // { "set_skybox", lua_set_skybox },
    // { "set_renderoption", lua_set_renderoption },
    {"get_units", lua_get_units},
    {"get_unitclass", lua_get_unitclass},

    {"set_pl3d_bound_min", lua_set_pl3d_bound_min},
    {"set_pl3d_bound_max", lua_set_pl3d_bound_max},

    {"set_units", lua_set_units},
    {"set_unitclasses", lua_set_unitclasses},
    {"set_zaxisangles", lua_set_zaxisangles},
    {"set_colorbartype", lua_set_colorbartype},
    {"set_extremecolors", lua_set_extremecolors},
    {"set_firecolor", lua_set_firecolor},
    {"set_firecolormap", lua_set_firecolormap},
    {"set_firedepth", lua_set_firedepth},
    // { "set_golorbar", lua_set_gcolorbar },
    {"set_showextremedata", lua_set_showextremedata},
    {"set_smokecolor", lua_set_smokecolor},
    {"set_smokecull", lua_set_smokecull},
    {"set_smokeskip", lua_set_smokeskip},
    {"set_smokealbedo", lua_set_smokealbedo},
#ifdef pp_GPU // TODO: register anyway, but tell user it is not available
    {"set_smokerthick", lua_set_smokerthick},
#endif
// { "set_smokethick", lua_set_smokethick },
#ifdef pp_GPU
    {"set_usegpu", lua_set_usegpu},
#endif
    {"set_showhazardcolors", lua_set_showhazardcolors},
    {"set_showhzone", lua_set_showhzone},
    {"set_showszone", lua_set_showszone},
    {"set_showvzone", lua_set_showvzone},
    {"set_showzonefire", lua_set_showzonefire},
    {"set_showpathnodes", lua_set_showpathnodes},
    {"set_showtourroute", lua_set_showtourroute},
    {"set_tourcolors_selectedpathline", lua_set_tourcolors_selectedpathline},
    {"set_tourcolors_selectedpathlineknots",
     lua_set_tourcolors_selectedpathlineknots},
    {"set_tourcolors_selectedknot", lua_set_tourcolors_selectedknot},
    {"set_tourcolors_pathline", lua_set_tourcolors_pathline},
    {"set_tourcolors_pathknots", lua_set_tourcolors_pathknots},
    {"set_tourcolors_text", lua_set_tourcolors_text},
    {"set_tourcolors_avatar", lua_set_tourcolors_avatar},
    {"set_viewalltours", lua_set_viewalltours},
    {"set_viewtimes", lua_set_viewtimes},
    {"set_viewtourfrompath", lua_set_viewtourfrompath},
    {"set_devicevectordimensions", lua_set_devicevectordimensions},
    {"set_devicebounds", lua_set_devicebounds},
    {"set_deviceorientation", lua_set_deviceorientation},
    {"set_gridparms", lua_set_gridparms},
    {"set_gsliceparms", lua_set_gsliceparms},
    {"set_loadfilesatstartup", lua_set_loadfilesatstartup},
    {"set_mscale", lua_set_mscale},
    {"set_sliceauto", lua_set_sliceauto},
    {"set_msliceauto", lua_set_msliceauto},
    {"set_compressauto", lua_set_compressauto},
    // { "set_part5propdisp", lua_set_part5propdisp },
    // { "set_part5color", lua_set_part5color },
    {"set_propindex", lua_set_propindex},
    // { "set_shooter", lua_set_shooter },
    {"set_showdevices", lua_set_showdevices},
    {"set_showdevicevals", lua_set_showdevicevals},
    {"set_showmissingobjects", lua_set_showmissingobjects},
    {"set_tourindex", lua_set_tourindex},
    // { "set_userticks", lua_set_userticks },
    {"set_c_particles", lua_set_c_particles},
    {"set_c_slice", lua_set_c_slice},
    {"set_cache_boundarydata", lua_set_cache_boundarydata},
    {"set_cache_qdata", lua_set_cache_qdata},
    {"set_percentilelevel", lua_set_percentilelevel},
    {"set_timeoffset", lua_set_timeoffset},
    {"set_tload", lua_set_tload},
    {"set_v_slice", lua_set_v_slice},
    {"set_patchdataout", lua_set_patchdataout},

    {"show_smoke3d_showall", lua_show_smoke3d_showall},
    {"show_smoke3d_hideall", lua_show_smoke3d_hideall},
    {"show_slices_showall", lua_show_slices_showall},
    {"show_slices_hideall", lua_show_slices_hideall},
    {"add_title_line", lua_add_title_line},
    {"clear_title_lines", lua_clear_title_lines},

    {"get_nglobal_times", lua_get_nglobal_times},
    {"get_global_time", lua_get_global_time},
    {"get_npartinfo", lua_get_npartinfo},

    {"get_slice", lua_get_slice},
    {"slice_get_label", lua_slice_get_label},
    {"slice_get_filename", lua_slice_get_filename},
    {"slice_get_data", lua_slice_get_data},
    {"slice_data_map_frames", lua_slice_data_map_frames},
    {"slice_data_map_frames_count_less", lua_slice_data_map_frames_count_less},
    {"slice_data_map_frames_count_less_eq",
     lua_slice_data_map_frames_count_less_eq},
    {"slice_data_map_frames_count_greater",
     lua_slice_data_map_frames_count_greater},
    {"slice_data_map_frames_count_greater_eq",
     lua_slice_data_map_frames_count_greater_eq},
    {"slice_get_times", lua_slice_get_times},

    {"get_part", lua_get_part},
    {"get_part_npoints", lua_get_part_npoints},

    {"get_qdata_sum", lua_get_qdata_sum},
    {"get_qdata_sum_bounded", lua_get_qdata_sum_bounded},
    {"get_qdata_max_bounded", lua_get_qdata_max_bounded},
    {"get_qdata_mean", lua_get_qdata_mean},
    // nglobal_times is the number of frames
    //  this cannot be set as a global at init as it will change
    //  on the loading of smokeview cases
    {NULL, NULL}};

int smvlib_newindex(lua_State *L) {
  const char *field = lua_tostring(L, 2);
  const char *value = lua_tostring(L, 3);
  if (strcmp(field, "renderdir") == 0) {
    lua_pushstring(L, value);
    lua_setrenderdir(L);
    return 0;
  } else {
    return 0;
  }
}

int smvlib_index(lua_State *L) {
  // Take the index from the table.
  // lua_pushstring(L, "index");
  // lua_gettable(L, 1);
  // int index = lua_tonumber(L, -1);
  const char *field = lua_tostring(L, 2);
  if (strcmp(field, "renderdir") == 0) {
    return lua_getrenderdir(L);
  } else {
    return 0;
  }
}

lua_State *initLua() {
  L = luaL_newstate();

  luaL_openlibs(L);

  luaL_newlib(L, smvlib);

  lua_pushcfunction(L, &lua_create_case);
  lua_setfield(L, -2, "load_default");

  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, &smvlib_newindex);
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, &smvlib_index);
  lua_setfield(L, -2, "__index");
  // then set the metatable
  lua_setmetatable(L, -2);

  lua_setglobal(L, "smvlib");

  lua_pushstring(L, script_dir_path);
  lua_setglobal(L, "current_script_dir");

  // a boolean value that determines if lua is running in smokeview
  lua_pushboolean(L, 1);
  lua_setglobal(L, "smokeviewEmbedded");

  return L;
}

int runScriptString(const char *string) { return luaL_dostring(L, string); }

int loadLuaScript(const char *filename) {
  // The display callback needs to be run once initially.
  // PROBLEM: the display CB does not work without a loaded case.
  runluascript = 0;
  lua_displayCB(L);
  runluascript = 1;
  char cwd[1000];
#if defined(_WIN32)
  _getcwd(cwd, 1000);
#else
  getcwd(cwd, 1000);
#endif
  const char *err_msg;
  lua_Debug info;
  int level = 0;
  int return_code = luaL_loadfile(L, filename);
  switch (return_code) {
  case LUA_OK:
    break;
  case LUA_ERRSYNTAX:
    fprintf(stderr, "Syntax error loading %s\n", filename);
    err_msg = lua_tostring(L, -1);
    fprintf(stderr, "error:%s\n", err_msg);
    level = 0;
    while (lua_getstack(L, level, &info)) {
      lua_getinfo(L, "nSl", &info);
      fprintf(stderr, "  [%d] %s:%d -- %s [%s]\n", level, info.short_src,
              info.currentline, (info.name ? info.name : "<unknown>"),
              info.what);
      ++level;
    }
    break;
  case LUA_ERRMEM:
    break;
  case LUA_ERRFILE:
    fprintf(stderr, "Could not load file %s\n", filename);
    err_msg = lua_tostring(L, -1);
    fprintf(stderr, "error:%s\n", err_msg);
    level = 0;
    while (lua_getstack(L, level, &info)) {
      lua_getinfo(L, "nSl", &info);
      fprintf(stderr, "  [%d] %s:%d -- %s [%s]\n", level, info.short_src,
              info.currentline, (info.name ? info.name : "<unknown>"),
              info.what);
      ++level;
    }
    break;
  }
  return return_code;
}

int loadSSFScript(const char *filename) {
  // char filename[1024];
  //   if (strlen(script_filename) == 0) {
  //       strncpy(filename, fdsprefix, 1020);
  //       strcat(filename, ".ssf");
  //   } else {
  //       strncpy(filename, script_filename, 1024);
  //   }
  // The display callback needs to be run once initially.
  // PROBLEM: the display CB does not work without a loaded case.
  runscript = 0;
  lua_displayCB(L);
  runscript = 1;
  const char *err_msg;
  lua_Debug info;
  int level = 0;
  char lString[1024];
  snprintf(lString, 1024, "require(\"ssfparser\")\nrunSSF(\"%s.ssf\")",
           fdsprefix);
  int ssfparser_loaded_err = luaL_dostring(L, "require \"ssfparser\"");
  if (ssfparser_loaded_err) {
    fprintf(stderr, "Failed to load ssfparser\n");
  }
  int return_code = luaL_loadstring(L, lString);
  switch (return_code) {
  case LUA_OK:
    break;
  case LUA_ERRSYNTAX:
    fprintf(stderr, "Syntax error loading %s\n", filename);
    err_msg = lua_tostring(L, -1);
    fprintf(stderr, "error:%s\n", err_msg);
    level = 0;
    while (lua_getstack(L, level, &info)) {
      lua_getinfo(L, "nSl", &info);
      fprintf(stderr, "  [%d] %s:%d -- %s [%s]\n", level, info.short_src,
              info.currentline, (info.name ? info.name : "<unknown>"),
              info.what);
      ++level;
    }
    break;
  case LUA_ERRMEM:
    break;
  case LUA_ERRFILE:
    fprintf(stderr, "Could not load file %s\n", filename);
    err_msg = lua_tostring(L, -1);
    fprintf(stderr, "error:%s\n", err_msg);
    level = 0;
    while (lua_getstack(L, level, &info)) {
      lua_getinfo(L, "nSl", &info);
      fprintf(stderr, "  [%d] %s:%d -- %s [%s]\n", level, info.short_src,
              info.currentline, (info.name ? info.name : "<unknown>"),
              info.what);
      ++level;
    }
    break;
  }
  return 0;
}

int yieldOrOkSSF = LUA_YIELD;
int runSSFScript() {
  if (yieldOrOkSSF == LUA_YIELD) {
    int nresults = 0;
#if LUA_VERSION_NUM < 502
    yieldOrOkSSF = lua_resume(L, 0);
#elif LUA_VERSION_NUM < 504
    yieldOrOkSSF = lua_resume(L, NULL, 0);
#else
    yieldOrOkSSF = lua_resume(L, NULL, 0, &nresults);
#endif
    if (yieldOrOkSSF == LUA_YIELD) {
      printf("  LUA_YIELD\n");
    } else if (yieldOrOkSSF == LUA_OK) {
      printf("  LUA_OK\n");
    } else if (yieldOrOkSSF == LUA_ERRRUN) {
      printf("  LUA_ERRRUN\n");
      const char *err_msg;
      err_msg = lua_tostring(L, -1);
      fprintf(stderr, "error:%s\n", err_msg);
      lua_Debug info;
      int level = 0;
      while (lua_getstack(L, level, &info)) {
        lua_getinfo(L, "nSl", &info);
        fprintf(stderr, "  [%d] %s:%d -- %s [%s]\n", level, info.short_src,
                info.currentline, (info.name ? info.name : "<unknown>"),
                info.what);
        ++level;
      };
    } else if (yieldOrOkSSF == LUA_ERRMEM) {
      printf("  LUA_ERRMEM\n");
    } else {
      printf("  resume code: %i\n", yieldOrOkSSF);
    }
  } else {
    lua_close(L);
    glutIdleFunc(NULL);
  }
  return yieldOrOkSSF;
}

int yieldOrOk = LUA_YIELD;
int runLuaScript() {
  if (yieldOrOk == LUA_YIELD) {
    int nresults = 0;
#if LUA_VERSION_NUM < 502
    yieldOrOk = lua_resume(L, 0);
#elif LUA_VERSION_NUM < 504
    yieldOrOk = lua_resume(L, NULL, 0);
#else
    yieldOrOk = lua_resume(L, NULL, 0, &nresults);
#endif
    if (yieldOrOk == LUA_YIELD) {
      printf("  LUA_YIELD\n");
    } else if (yieldOrOk == LUA_OK) {
      printf("  LUA_OK\n");
    } else if (yieldOrOk == LUA_ERRRUN) {
      printf("  LUA_ERRRUN\n");
      const char *err_msg;
      err_msg = lua_tostring(L, -1);
      fprintf(stderr, "error:%s\n", err_msg);
      lua_Debug info;
      int level = 0;
      while (lua_getstack(L, level, &info)) {
        lua_getinfo(L, "nSl", &info);
        fprintf(stderr, "  [%d] %s:%d -- %s [%s]\n", level, info.short_src,
                info.currentline, (info.name ? info.name : "<unknown>"),
                info.what);
        ++level;
      };
    } else if (yieldOrOk == LUA_ERRMEM) {
      printf("  LUA_ERRMEM\n");
    } else {
      printf("  resume code: %i\n", yieldOrOk);
    }
  } else {
    lua_close(L);
    glutIdleFunc(NULL);
  }
  GLUTPOSTREDISPLAY;
  return yieldOrOk;
}
#if LUA_VERSION_NUM < 502
LUA_API lua_Number lua_version(lua_State *L) {
  UNUSED(L);
  return LUA_VERSION_NUM;
}

LUALIB_API void luaL_checkversion_(lua_State *L, lua_Number ver, size_t sz) {
  lua_Number v = lua_version(L);
  if (sz != LUAL_NUMSIZES) /* check numeric types */
    luaL_error(L, "core and library have incompatible numeric types");
  else if (v != ver)
    luaL_error(L, "version mismatch: app. needs %f, Lua core provides %f",
               (LUAI_UACNUMBER)ver, (LUAI_UACNUMBER)v);
}

LUALIB_API void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup) {
  luaL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) { /* fill the table with given functions */
    if (l->func == NULL)         /* place holder? */
      lua_pushboolean(L, 0);
    else {
      int i;
      for (i = 0; i < nup; i++) /* copy upvalues to the top */
        lua_pushvalue(L, -nup);
      lua_pushcclosure(L, l->func, nup); /* closure with those upvalues */
    }
    lua_setfield(L, -(nup + 2), l->name);
  }
  lua_pop(L, nup); /* remove upvalues */
}
#endif
#endif
