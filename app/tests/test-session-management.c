/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 2009 Martin Nordholts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gegl.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <utime.h>

#include "libgimpbase/gimpbase.h"

#include "dialogs/dialogs-types.h"

#include "dialogs/dialogs.h"

#include "widgets/gimpdialogfactory.h"
#include "widgets/gimpsessioninfo.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


typedef struct
{
  int dummy;
} GimpTestFixture;

typedef struct
{
  gchar    *md5;
  GTimeVal  modtime;
} GimpTestFileState;


static gboolean  gimp_test_get_file_state_verbose (gchar             *filename,
                                                   GimpTestFileState *filestate);
static gboolean  gimp_test_file_state_changes     (gchar             *filename,
                                                   GimpTestFileState *state1,
                                                   GimpTestFileState *state2);

static Gimp *gimp = NULL;


int main(int argc, char **argv)
{
  GimpTestFileState  initial_sessionrc_state = { NULL, { 0, 0 } };
  GimpTestFileState  initial_dockrc_state    = { NULL, { 0, 0 } };
  GimpTestFileState  final_sessionrc_state   = { NULL, { 0, 0 } };
  GimpTestFileState  final_dockrc_state      = { NULL, { 0, 0 } };
  gchar             *sessionrc_filename      = NULL;
  gchar             *dockrc_filename         = NULL;

  g_type_init ();
  gtk_init (&argc, &argv);
  g_test_init (&argc, &argv, NULL);

  /* Make sure to run this before we use any GIMP functions */
  gimp_test_utils_set_gimp2_directory ("gimpdir");

  sessionrc_filename = gimp_personal_rc_file ("sessionrc");
  dockrc_filename    = gimp_personal_rc_file ("dockrc");

  /* Remeber the modtimes and MD5s */
  if (!gimp_test_get_file_state_verbose (sessionrc_filename,
                                         &initial_sessionrc_state))
    goto fail;
  if (!gimp_test_get_file_state_verbose (dockrc_filename,
                                         &initial_dockrc_state))
    goto fail;

  /* Start up GIMP */
  gimp = gimp_init_for_gui_testing (FALSE, TRUE);

  /* Let the main loop run for a while (quits after a short timeout)
   * to let things stabilize. This includes parsing sessionrc and
   * dockrc
   */
  gimp_test_run_mainloop_until_idle ();

  /* Exit. This includes writing sessionrc and dockrc*/
  gimp_exit (gimp, TRUE);

  /* Now get the new modtimes and MD5s */
  if (!gimp_test_get_file_state_verbose (sessionrc_filename,
                                         &final_sessionrc_state))
    goto fail;
  if (!gimp_test_get_file_state_verbose (dockrc_filename,
                                         &final_dockrc_state))
    goto fail;

  /* If things have gone our way, GIMP will have deserialized
   * sessionrc and dockrc, shown the GUI, and then serialized the new
   * files. To make sure we have new files we check the modtime, and
   * to make sure that their content remains the same we compare their
   * MD5
   */
  if (!gimp_test_file_state_changes ("sessionrc",
                                     &initial_sessionrc_state,
                                     &final_sessionrc_state))
    goto fail;
  if (!gimp_test_file_state_changes ("dockrc",
                                     &initial_dockrc_state,
                                     &final_dockrc_state))
    goto fail;

  /* Don't bother freeing stuff, the process is short-lived */
  return 0;

 fail:
  return -1;
}

static gboolean
gimp_test_get_file_state_verbose (gchar             *filename,
                                  GimpTestFileState *filestate)
{
  gboolean success = TRUE;

  /* Get checksum */
  if (success)
    {
      gchar *contents = NULL;
      gsize  length   = 0;

      success = g_file_get_contents (filename,
                                     &contents,
                                     &length,
                                     NULL);
      if (success)
        {
          filestate->md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5,
                                                          contents,
                                                          length);
        }

      g_free (contents);
    }

  /* Get modification time */
  if (success)
    {
      GFile     *file = g_file_new_for_path (filename);
      GFileInfo *info = g_file_query_info (file,
                                           G_FILE_ATTRIBUTE_TIME_MODIFIED, 0,
                                           NULL, NULL);
      if (info)
        {
          g_file_info_get_modification_time (info, &filestate->modtime);
          success = TRUE;
          g_object_unref (info);
        }
      else
        {
          success = FALSE;
        }

      g_object_unref (file);
    }

  if (! success)
    g_printerr ("Failed to get initial file info for '%s'\n", filename);

  return success;
}

static gboolean
gimp_test_file_state_changes (gchar             *filename,
                              GimpTestFileState *state1,
                              GimpTestFileState *state2)
{
  if (state1->modtime.tv_sec  == state2->modtime.tv_sec &&
      state1->modtime.tv_usec == state2->modtime.tv_usec)
    {
      g_printerr ("A new '%s' was not created\n", filename);
      return FALSE;
    }

  if (strcmp (state1->md5, state2->md5) != 0)
    {
      g_printerr ("'%s' was changed but should not have been\n", filename);
      return FALSE;
    }

  return TRUE;
}