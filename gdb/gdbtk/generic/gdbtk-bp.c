/* Tcl/Tk command definitions for Insight - Breakpoints.
   Copyright (C) 2001-2012 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "defs.h"
#include "symtab.h"
#include "symfile.h"
#include "source.h"
#include "linespec.h"
#include "breakpoint.h"
#include "tracepoint.h"
#include "gdb_string.h"
#include <tcl.h>
#include "gdbtk.h"
#include "gdbtk-cmds.h"
#include "observer.h"
#include "arch-utils.h"
#include "exceptions.h"

/* Globals to support action and breakpoint commands.  */
static Tcl_Obj **gdbtk_obj_array;
static int gdbtk_obj_array_cnt;
static int gdbtk_obj_array_ptr;

/* From breakpoint.c */
extern struct breakpoint *breakpoint_chain;

#define ALL_BREAKPOINTS(B)  for (B = breakpoint_chain; B; B = B->next)

/* From gdbtk-hooks.c */
extern void report_error (void);

/* These two lookup tables are used to translate the type & disposition fields
   of the breakpoint structure (respectively) into something gdbtk understands.
   They are also used in gdbtk-hooks.c */

char *bptypes[] =
  {"none", "breakpoint", "hw breakpoint", "until",
   "finish", "watchpoint", "hw watchpoint",
   "read watchpoint", "acc watchpoint",
   "longjmp", "longjmp resume", "step resume",
   "sigtramp", "watchpoint scope",
   "call dummy", "shlib events", "catch load",
   "catch unload", "catch fork", "catch vfork",
   "catch exec", "catch catch", "catch throw"
  };
char *bpdisp[] =
  {"delete", "delstop", "disable", "donttouch"};

/* Is this breakpoint interesting to a user interface? */
#define BREAKPOINT_IS_INTERESTING(bp) \
((bp)->type == bp_breakpoint             \
 || (bp)->type == bp_hardware_breakpoint \
 || (bp)->type == bp_watchpoint          \
 || (bp)->type == bp_hardware_watchpoint \
 || (bp)->type == bp_read_watchpoint     \
 || (bp)->type == bp_access_watchpoint)

/*
 * Forward declarations
 */

/* Breakpoint-related functions */
static int gdb_find_bp_at_addr (ClientData, Tcl_Interp *, int,
				Tcl_Obj * CONST objv[]);
static int gdb_find_bp_at_line (ClientData, Tcl_Interp *, int,
				Tcl_Obj * CONST objv[]);
static int gdb_get_breakpoint_info (ClientData, Tcl_Interp *, int,
				    Tcl_Obj * CONST[]);
static int gdb_get_breakpoint_list (ClientData, Tcl_Interp *, int,
				    Tcl_Obj * CONST[]);
static int gdb_set_bp (ClientData, Tcl_Interp *, int, Tcl_Obj * CONST objv[]);

/* Tracepoint-related functions */
static int gdb_actions_command (ClientData, Tcl_Interp *, int,
				Tcl_Obj * CONST objv[]);
static int gdb_get_trace_frame_num (ClientData, Tcl_Interp *, int,
				    Tcl_Obj * CONST objv[]);
static int gdb_get_tracepoint_info (ClientData, Tcl_Interp *, int,
				    Tcl_Obj * CONST objv[]);
static int gdb_get_tracepoint_list (ClientData, Tcl_Interp *, int,
				    Tcl_Obj * CONST objv[]);
static int gdb_trace_status (ClientData, Tcl_Interp *, int,
			     Tcl_Obj * CONST[]);
static int gdb_tracepoint_exists_command (ClientData, Tcl_Interp *,
					  int, Tcl_Obj * CONST objv[]);
static Tcl_Obj *get_breakpoint_commands (struct command_line *cmd);

static int tracepoint_exists (char *args);

/* Breakpoint/tracepoint events and related functions */

void gdbtk_create_breakpoint (struct breakpoint *);
void gdbtk_delete_breakpoint (struct breakpoint *);
void gdbtk_modify_breakpoint (struct breakpoint *);
void gdbtk_create_tracepoint (int);
void gdbtk_delete_tracepoint (int);
void gdbtk_modify_tracepoint (int);
static void breakpoint_notify (int, const char *);
static void tracepoint_notify (int, const char *);

int
Gdbtk_Breakpoint_Init (Tcl_Interp *interp)
{
  /* Breakpoint commands */
  Tcl_CreateObjCommand (interp, "gdb_find_bp_at_addr", gdbtk_call_wrapper,
			gdb_find_bp_at_addr, NULL);
  Tcl_CreateObjCommand (interp, "gdb_find_bp_at_line", gdbtk_call_wrapper,
			gdb_find_bp_at_line, NULL);
  Tcl_CreateObjCommand (interp, "gdb_get_breakpoint_info", gdbtk_call_wrapper,
			gdb_get_breakpoint_info, NULL);
  Tcl_CreateObjCommand (interp, "gdb_get_breakpoint_list", gdbtk_call_wrapper,
			gdb_get_breakpoint_list, NULL);
  Tcl_CreateObjCommand (interp, "gdb_set_bp", gdbtk_call_wrapper, gdb_set_bp, NULL);

  /* Tracepoint commands */
  Tcl_CreateObjCommand (interp, "gdb_actions",
			gdbtk_call_wrapper, gdb_actions_command, NULL);
  Tcl_CreateObjCommand (interp, "gdb_get_trace_frame_num",
			gdbtk_call_wrapper, gdb_get_trace_frame_num, NULL);
  Tcl_CreateObjCommand (interp, "gdb_get_tracepoint_info",
			gdbtk_call_wrapper, gdb_get_tracepoint_info, NULL);
  Tcl_CreateObjCommand (interp, "gdb_get_tracepoint_list",
			gdbtk_call_wrapper, gdb_get_tracepoint_list, NULL);
  Tcl_CreateObjCommand (interp, "gdb_is_tracing",
			gdbtk_call_wrapper, gdb_trace_status,	NULL);
  Tcl_CreateObjCommand (interp, "gdb_tracepoint_exists",
			gdbtk_call_wrapper, gdb_tracepoint_exists_command, NULL);

  return TCL_OK;
}

/* A line buffer for breakpoint commands and tracepoint actions
   input validation.  */
static char *
gdbtk_read_next_line (void)
{
  if (gdbtk_obj_array_ptr == gdbtk_obj_array_cnt)
    return NULL;

  return  Tcl_GetStringFromObj (gdbtk_obj_array[gdbtk_obj_array_ptr++], NULL);
}

/*
 *  This section contains commands for manipulation of breakpoints.
 */

/* set a breakpoint by source file and line number
   flags are as follows:
   least significant 2 bits are disposition, rest is 
   type (normally 0).

   enum bptype {
   bp_breakpoint,                Normal breakpoint 
   bp_hardware_breakpoint,      Hardware assisted breakpoint
   }

   Disposition of breakpoint.  Ie: what to do after hitting it.
   enum bpdisp {
   del,                         Delete it
   del_at_next_stop,            Delete at next stop, whether hit or not
   disable,                     Disable it 
   donttouch                    Leave it alone 
   };
*/


/* This implements the tcl command "gdb_find_bp_at_addr"

* Tcl Arguments:
*    addr:     CORE_ADDR
* Tcl Result:
*    It returns a list of breakpoint numbers
*/
static int
gdb_find_bp_at_addr (ClientData clientData, Tcl_Interp *interp,
		     int objc, Tcl_Obj *CONST objv[])
{
  CORE_ADDR addr;
  Tcl_WideInt waddr;
  struct breakpoint *b;

  if (objc != 2)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "address");
      return TCL_ERROR;
    }
  
  if (Tcl_GetWideIntFromObj (interp, objv[1], &waddr) != TCL_OK)
    return TCL_ERROR;
  addr = waddr;

  Tcl_SetListObj (result_ptr->obj_ptr, 0, NULL);
  ALL_BREAKPOINTS (b)
    {
      if (b->loc != NULL && b->loc->address == addr)
	Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
				  Tcl_NewIntObj (b->number));
    }

  return TCL_OK;
}

/* This implements the tcl command "gdb_find_bp_at_line"

* Tcl Arguments:
*    filename: the file in which to find the breakpoint
*    line:     the line number for the breakpoint
* Tcl Result:
*    It returns a list of breakpoint numbers
*/
static int
gdb_find_bp_at_line (ClientData clientData, Tcl_Interp *interp,
		     int objc, Tcl_Obj *CONST objv[])

{
  struct symtab *s;
  int line;
  struct breakpoint *b;

  if (objc != 3)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "filename line");
      return TCL_ERROR;
    }

  s = lookup_symtab (Tcl_GetStringFromObj (objv[1], NULL));
  if (s == NULL)
    return TCL_ERROR;

  if (Tcl_GetIntFromObj (interp, objv[2], &line) == TCL_ERROR)
    {
      result_ptr->flags = GDBTK_IN_TCL_RESULT;
      return TCL_ERROR;
    }

  Tcl_SetListObj (result_ptr->obj_ptr, 0, NULL);
  ALL_BREAKPOINTS (b)
  {
    if (b->loc && b->loc->line_number == line
	&& !strcmp (b->loc->source_file, s->filename))
      {
	Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
				  Tcl_NewIntObj (b->number));
      }
  }

  return TCL_OK;
}

/* This implements the tcl command gdb_get_breakpoint_info
 *
 * Tcl Arguments:
 *   breakpoint_number
 * Tcl Result:
 *   A list with {file, function, line_number, address, type, enabled?,
 *                disposition, ignore_count, {list_of_commands},
 *                condition, thread, hit_count user_specification}
 */
static int
gdb_get_breakpoint_info (ClientData clientData, Tcl_Interp *interp, int objc,
			 Tcl_Obj *CONST objv[])
{
  struct symtab_and_line sal;
  int bpnum;
  struct breakpoint *b;
  struct watchpoint *w;
  const char *funcname, *filename;
  int isPending = 0;

  Tcl_Obj *new_obj;

  if (objc != 2)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "breakpoint");
      return TCL_ERROR;
    }

  if (Tcl_GetIntFromObj (NULL, objv[1], &bpnum) != TCL_OK)
    {
      result_ptr->flags = GDBTK_IN_TCL_RESULT;
      return TCL_ERROR;
    }

  b = get_breakpoint (bpnum);
  if (!b || b->type != bp_breakpoint)
    {
      gdbtk_set_result (interp, "Breakpoint #%d does not exist.", bpnum);
      return TCL_ERROR;
    }

  w = (is_watchpoint (b)) ? (struct watchpoint *) b : NULL;

  isPending = (b->loc == NULL);
  Tcl_SetListObj (result_ptr->obj_ptr, 0, NULL);
  /* Pending breakpoints will display "<PENDING>" as the file name and the 
     user expression into the Function field of the breakpoint view.
    "0" and "0" in the line number and address field.  */
  if (isPending)
    {
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewStringObj ("<PENDING>", -1));
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewStringObj (b->addr_string, -1));
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewIntObj (0));
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewIntObj (0));
    }
  else
    {
      sal = find_pc_line (b->loc->address, 0);

      filename = symtab_to_filename (sal.symtab);
      if (filename == NULL)
        filename = "";
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewStringObj (filename, -1));
      funcname = pc_function_name (b->loc->address);
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewStringObj (funcname, -1));
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewIntObj (b->loc->line_number));
      Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
                                Tcl_NewStringObj (core_addr_to_string
                               (b->loc->address), -1));
  }

  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewStringObj (bptypes[b->type], -1));
  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewBooleanObj (b->enable_state == bp_enabled));
  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewStringObj (bpdisp[b->disposition], -1));
  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewIntObj (b->ignore_count));

  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    get_breakpoint_commands ((breakpoint_commands (b)) ? breakpoint_commands (b) : NULL));

  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewStringObj (b->cond_string, -1));

  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewIntObj (b->thread));
  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewIntObj (b->hit_count));

  Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr,
			    Tcl_NewStringObj (w ? w->exp_string
					      : b->addr_string, -1));

  return TCL_OK;
}

/* Helper function for gdb_get_breakpoint_info, this function is
   responsible for figuring out what to type at the "commands" command
   in gdb's cli in order to get at the same command list passed here. */

static Tcl_Obj *
get_breakpoint_commands (struct command_line *cmd)
{
  Tcl_Obj *obj, *tmp;

  obj = Tcl_NewObj ();
  while (cmd != NULL)
    {
      switch (cmd->control_type)
	{
	case simple_control:
	  /* A simple command. Just append it. */
	  Tcl_ListObjAppendElement (NULL, obj,
				    Tcl_NewStringObj (cmd->line, -1));
	  break;

	case break_control:
	  /* A loop_break */
	  Tcl_ListObjAppendElement (NULL, obj,
				    Tcl_NewStringObj ("loop_break", -1));
	  break;

	case continue_control:
	  /* A loop_continue */
	  Tcl_ListObjAppendElement (NULL, obj,
				    Tcl_NewStringObj ("loop_continue", -1));
	  break;

	case while_control:
	  /* A while loop. Must append "end" to the end of it. */
	  tmp = Tcl_NewStringObj ("while ", -1);
	  Tcl_AppendToObj (tmp, cmd->line, -1);
	  Tcl_ListObjAppendElement (NULL, obj, tmp);
	  Tcl_ListObjAppendList (NULL, obj,
				 get_breakpoint_commands (*cmd->body_list));
	  Tcl_ListObjAppendElement (NULL, obj,
				    Tcl_NewStringObj ("end", -1));
	  break;

	case if_control:
	  /* An if statement. cmd->body_list[0] is the true part,
	     cmd->body_list[1] contains the "else" (false) part. */
	  tmp = Tcl_NewStringObj ("if ", -1);
	  Tcl_AppendToObj (tmp, cmd->line, -1);
	  Tcl_ListObjAppendElement (NULL, obj, tmp);
	  Tcl_ListObjAppendList (NULL, obj,
				 get_breakpoint_commands (cmd->body_list[0]));
	  if (cmd->body_count == 2)
	    {
	      Tcl_ListObjAppendElement (NULL, obj,
					Tcl_NewStringObj ("else", -1));
	      Tcl_ListObjAppendList (NULL, obj,
				     get_breakpoint_commands(cmd->body_list[1]));
	    }
	  Tcl_ListObjAppendElement (NULL, obj,
				    Tcl_NewStringObj ("end", -1));
	  break;

	case invalid_control:
	  /* Something invalid. Just skip it. */
	  break;
	}

      cmd = cmd->next;
    }

  return obj;
}

/* This implements the tcl command gdb_get_breakpoint_list
 * It builds up a list of the current breakpoints.
 *
 * Tcl Arguments:
 *    None.
 * Tcl Result:
 *    A list of breakpoint numbers.
 */
static int
gdb_get_breakpoint_list (ClientData clientData, Tcl_Interp *interp,
			 int objc, Tcl_Obj *CONST objv[])
{
  Tcl_Obj *new_obj;
  struct breakpoint *b;

  if (objc != 1)
    {
      Tcl_WrongNumArgs (interp, 1, objv, NULL);
      return TCL_ERROR;
    }

  ALL_BREAKPOINTS (b)
  {
    if (b->type == bp_breakpoint)
      {
	new_obj = Tcl_NewIntObj (b->number);
	Tcl_ListObjAppendElement (NULL, result_ptr->obj_ptr, new_obj);
      }
  }

  return TCL_OK;
}

/* This implements the tcl command "gdb_set_bp"
 * It sets breakpoints, and notifies the GUI.
 *
 * Tcl Arguments:
 *    addr:     the "address" for the breakpoint (either *ADDR or file:line)
 *    type:     the type of the breakpoint
 *    thread:   optional thread number
 * Tcl Result:
 *    The return value of the call to gdbtk_tcl_breakpoint.
 */
static int
gdb_set_bp (ClientData clientData, Tcl_Interp *interp,
	    int objc, Tcl_Obj *CONST objv[])
{
  int temp, ignore_count, thread, pending, enabled;
  char *address, *typestr, *condition;
  struct gdb_exception e;

  /* Insight does not use all of these (yet?).  */
  ignore_count = 0;
  condition = NULL;
  pending = 0;
  enabled = 1;

  if (objc != 3 && objc != 4)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "addr type ?thread?");
      return TCL_ERROR;
    }

  address = Tcl_GetStringFromObj (objv[1], NULL);
  if (address == NULL)
    {
      result_ptr->flags = GDBTK_IN_TCL_RESULT;
      return TCL_ERROR;
    }

  typestr = Tcl_GetStringFromObj (objv[2], NULL);
  if (strncmp (typestr, "temp", 4) == 0)
    temp = 1;
  else if (strncmp (typestr, "normal", 6) == 0)
    temp = 0;
  else
    {
      gdbtk_set_result (interp, "type must be \"temp\" or \"normal\"");
      return TCL_ERROR;
    }

  if (objc == 4)
    {
      if (Tcl_GetIntFromObj (interp, objv[3], &thread) == TCL_ERROR)
	{
	  result_ptr->flags = GDBTK_IN_TCL_RESULT;
	  return TCL_ERROR;
	}
    }

  TRY_CATCH (e, RETURN_MASK_ALL)
    {
      create_breakpoint (get_current_arch (), address, condition, thread,
			 0	/* condition and thread are valid */,
			 temp,
			 bp_breakpoint /* type wanted */,
			 ignore_count,
			 (pending ? AUTO_BOOLEAN_TRUE : AUTO_BOOLEAN_FALSE),
			 &bkpt_breakpoint_ops,
			 0	/* from_tty */,
			 enabled, 0, 0);
    }

  if (e.reason < 0)
    return TCL_ERROR;

  return TCL_OK;
}

/*
 * This section contains functions that deal with breakpoint
 * events from gdb.
 */

/* The next three functions use breakpoint_notify to allow the GUI 
 * to handle creating, deleting and modifying breakpoints.  These three
 * functions are put into the appropriate gdb hooks in gdbtk_init.
 */

void
gdbtk_create_breakpoint (struct breakpoint *b)
{
  if (b == NULL || !BREAKPOINT_IS_INTERESTING (b))
    return;

  breakpoint_notify (b->number, "create");
}

void
gdbtk_delete_breakpoint (struct breakpoint *b)
{
  breakpoint_notify (b->number, "delete");
}

void
gdbtk_modify_breakpoint (struct breakpoint *b)
{
  if (b->number >= 0)
    breakpoint_notify (b->number, "modify");
}

/* This is the generic function for handling changes in
 * a breakpoint.  It routes the information to the Tcl
 * command "gdbtk_tcl_breakpoint" in the form:
 *   gdbtk_tcl_breakpoint action b_number b_address b_line b_file
 * On error, the error string is written to gdb_stdout.
 */
static void
breakpoint_notify (int num, const char *action)
{
  char *buf;
  struct breakpoint *b;

  b = get_breakpoint (num);

  if (b->number < 0
      /* FIXME: should not be so restrictive... */
      || b->type != bp_breakpoint)
    return;

  /* We ensure that ACTION contains no special Tcl characters, so we
     can do this.  */
  buf = xstrprintf ("gdbtk_tcl_breakpoint %s %d", action, b->number);

  if (Tcl_Eval (gdbtk_interp, buf) != TCL_OK)
    report_error ();
  xfree(buf); 
}

/*
 * This section contains the commands that deal with tracepoints:
 */

/* This implements the tcl command gdb_actions
 * It sets actions for a given tracepoint.
 *
 * Tcl Arguments:
 *    number: the tracepoint in question
 *    actions: the actions to add to this tracepoint
 * Tcl Result:
 *    None.
 */

static int
gdb_actions_command (ClientData clientData, Tcl_Interp *interp,
		     int objc, Tcl_Obj *CONST objv[])
{
  int tpnum;
  struct tracepoint *tp;
  struct command_line *commands;

  if (objc != 3)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "number actions");
      return TCL_ERROR;
    }

  if (Tcl_GetIntFromObj (NULL, objv[1], &tpnum) != TCL_OK)
    {
      result_ptr->flags |= GDBTK_IN_TCL_RESULT;
      return TCL_ERROR;
    }

  tp = get_tracepoint (tpnum);

  if (tp == NULL)
    {
      gdbtk_set_result (interp, "Tracepoint #%d does not exist", tpnum);
      return TCL_ERROR;
    }

  /* Validate and set new tracepoint actions.  */
  Tcl_ListObjGetElements (interp, objv[2], &gdbtk_obj_array_cnt,
			  &gdbtk_obj_array);
  gdbtk_obj_array_ptr = 1;
  commands = read_command_lines_1 (gdbtk_read_next_line, 1,
				   check_tracepoint_command, tp);  

  breakpoint_set_commands ((struct breakpoint *) tp, commands);
  return TCL_OK;
}

static int
gdb_get_trace_frame_num (ClientData clientData, Tcl_Interp *interp,
			 int objc, Tcl_Obj *CONST objv[])
{
  if (objc != 1)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "linespec");
      return TCL_ERROR;
    }

  Tcl_SetIntObj (result_ptr->obj_ptr, get_traceframe_number ());
  return TCL_OK;

}

static int
gdb_get_tracepoint_info (ClientData clientData, Tcl_Interp *interp,
			 int objc, Tcl_Obj *CONST objv[])
{
  struct symtab_and_line sal;
  int tpnum;
  struct tracepoint *tp;
  struct breakpoint *bp;
  struct command_line *cl;
  Tcl_Obj *action_list;
  const char *filename, *funcname;

  if (objc != 2)
    {
      Tcl_WrongNumArgs (interp, 1, objv, "tpnum");
      return TCL_ERROR;
    }

  if (Tcl_GetIntFromObj (NULL, objv[1], &tpnum) != TCL_OK)
    {
      result_ptr->flags |= GDBTK_IN_TCL_RESULT;
      return TCL_ERROR;
    }

  tp = get_tracepoint (tpnum);
  bp = (struct breakpoint *) tp;
  if (tp == NULL)
    {
      gdbtk_set_result (interp, "Tracepoint #%d does not exist", tpnum);
      return TCL_ERROR;
    }

  Tcl_SetListObj (result_ptr->obj_ptr, 0, NULL);
  sal = find_pc_line (bp->loc->address, 0);
  filename = symtab_to_filename (sal.symtab);
  if (filename == NULL)
    filename = "N/A";
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewStringObj (filename, -1));

  funcname = pc_function_name (bp->loc->address);
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr, Tcl_NewStringObj
			    (funcname, -1));

  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewIntObj (sal.line));
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewStringObj (core_addr_to_string (bp->loc->address), -1));
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewIntObj (bp->enable_state == bp_enabled));
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewIntObj (tp->pass_count));
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewIntObj (tp->step_count));
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewIntObj (bp->thread));
  Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			    Tcl_NewIntObj (bp->hit_count));

  /* Append a list of actions */
  action_list = Tcl_NewObj ();
  if (bp->commands != NULL)
    {
      for (cl = breakpoint_commands (bp); cl != NULL; cl = cl->next)
	{
	  Tcl_ListObjAppendElement (interp, action_list,
				    Tcl_NewStringObj (cl->line, -1));
	}
      Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr, action_list);
    }

  return TCL_OK;
}

/* return a list of all tracepoint numbers in interpreter */
static int
gdb_get_tracepoint_list (ClientData clientData,
			 Tcl_Interp *interp,
			 int objc,
			 Tcl_Obj *CONST objv[])
{
  VEC(breakpoint_p) *tp_vec = NULL;
  int ix;
  struct breakpoint *tp;

  Tcl_SetListObj (result_ptr->obj_ptr, 0, NULL);

  tp_vec = all_tracepoints ();
  for (ix = 0; VEC_iterate (breakpoint_p, tp_vec, ix, tp); ix++)
    Tcl_ListObjAppendElement (interp, result_ptr->obj_ptr,
			      Tcl_NewIntObj (tp->number));
  VEC_free (breakpoint_p, tp_vec);

  return TCL_OK;
}

static int
gdb_trace_status (ClientData clientData,
		  Tcl_Interp *interp,
		  int objc,
		  Tcl_Obj *CONST objv[])
{
  int result = 0;

  if (current_trace_status ()->running)
    result = 1;

  Tcl_SetIntObj (result_ptr->obj_ptr, result);
  return TCL_OK;
}

/* returns -1 if not found, tracepoint # if found */
static int
tracepoint_exists (char *args)
{
  VEC(breakpoint_p) *tp_vec = NULL;
  int ix;
  struct breakpoint *tp;
  struct symtabs_and_lines sals;
  char *file = NULL;
  int result = -1;

  sals = decode_line_1 (&args, DECODE_LINE_FUNFIRSTLINE, NULL, 0);
  if (sals.nelts == 1)
    {
      resolve_sal_pc (&sals.sals[0]);
      file = xmalloc (strlen (sals.sals[0].symtab->dirname)
		      + strlen (sals.sals[0].symtab->filename) + 1);
      if (file != NULL)
	{
	  strcpy (file, sals.sals[0].symtab->dirname);
	  strcat (file, sals.sals[0].symtab->filename);

	  tp_vec = all_tracepoints ();
	  for (ix = 0; VEC_iterate (breakpoint_p, tp_vec, ix, tp); ix++)
	    {
	      if (tp->loc && tp->loc->address == sals.sals[0].pc)
		result = tp->number;
#if 0
	      /* Why is this here? This messes up assembly traces */
	      else if (tp->source_file != NULL
		       && strcmp (tp->source_file, file) == 0
		       && sals.sals[0].line == tp->line_number)
		result = tp->number;
#endif
	    }
	  VEC_free (breakpoint_p, tp_vec);
	}
    }
  if (file != NULL)
    free (file);
  return result;
}

static int
gdb_tracepoint_exists_command (ClientData clientData,
			       Tcl_Interp *interp,
			       int objc,
			       Tcl_Obj *CONST objv[])
{
  char *args;

  if (objc != 2)
    {
      Tcl_WrongNumArgs (interp, 1, objv,
			"function:line|function|line|*addr");
      return TCL_ERROR;
    }

  args = Tcl_GetStringFromObj (objv[1], NULL);

  Tcl_SetIntObj (result_ptr->obj_ptr, tracepoint_exists (args));
  return TCL_OK;
}

/*
 * This section contains functions which deal with tracepoint
 * events from gdb.
 */

void
gdbtk_create_tracepoint (int num)
{
  tracepoint_notify (num, "create");
}

void
gdbtk_delete_tracepoint (int num)
{
  tracepoint_notify (num, "delete");
}

void
gdbtk_modify_tracepoint (int num)
{
  tracepoint_notify (num, "modify");
}

static void
tracepoint_notify (int num, const char *action)
{
  char *buf;

  /* We ensure that ACTION contains no special Tcl characters, so we
     can do this.  */
  buf = xstrprintf ("gdbtk_tcl_tracepoint %s %d", action, num);

  if (Tcl_Eval (gdbtk_interp, buf) != TCL_OK)
    report_error ();
  free(buf); 
}
