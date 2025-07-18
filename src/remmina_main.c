/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009-2011 Vic Lee
 * Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
 * Copyright (C) 2016-2022 Antenore Gatta, Giovanni Panozzo
 * Copyright (C) 2022-2023 Antenore Gatta, Giovanni Panozzo, Hiroyuki Tanaka
 * Copyright (C) 2023-2024 Hiroyuki Tanaka, Sunil Bhat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU General Public License in all respects
 *  for all of the code used other than OpenSSL. *  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so. *  If you
 *  do not wish to do so, delete this exception statement from your
 *  version. *  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 *
 */

#include "config.h"
#include <ctype.h>
#include <gio/gio.h>
#ifndef __APPLE__
#include <gio/gdesktopappinfo.h>
#endif
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "remmina.h"
#include "remmina_string_array.h"
#include "remmina_public.h"
#include "remmina_file.h"
#include "remmina_file_manager.h"
#include "remmina_file_editor.h"
#include "rcw.h"
#include "remmina_about.h"
#include "remmina_pref.h"
#include "remmina_pref_dialog.h"
#include "remmina_widget_pool.h"
#include "remmina_plugin_manager.h"
#include "remmina_bug_report.h"
#include "remmina_log.h"
#include "remmina_icon.h"
#include "remmina_main.h"
#include "remmina_exec.h"
#include "remmina_mpchange.h"
#include "remmina_external_tools.h"
#include "remmina_unlock.h"
#include "remmina/remmina_trace_calls.h"

static RemminaMain *remminamain;

#define RM_GET_OBJECT(object_name) gtk_builder_get_object(remminamain->builder, object_name)

enum {
	PROTOCOL_COLUMN,
	NAME_COLUMN,
	GROUP_COLUMN,
	SERVER_COLUMN,
	PLUGIN_COLUMN,
	DATE_COLUMN,
	FILENAME_COLUMN,
	LABELS_COLUMN,
	NOTES_COLUMN,
	STATUS_COLUMN,
	N_COLUMNS
};

static
const gchar *supported_mime_types[] = {
	"x-scheme-handler/rdp",
	"x-scheme-handler/spice",
	"x-scheme-handler/vnc",
	"x-scheme-handler/remmina",
	"application/x-remmina",
	NULL
};

static GActionEntry app_actions[] = {
	{ "about",	 remmina_main_on_action_application_about,	 NULL, NULL, NULL },
	{ "default",	 remmina_main_on_action_application_default,	 NULL, NULL, NULL },
	{ "mpchange",	 remmina_main_on_action_application_mpchange,	 NULL, NULL, NULL },
	{ "plugins",	 remmina_main_on_action_application_plugins,	 NULL, NULL, NULL },
	{ "preferences", remmina_main_on_action_application_preferences, "i",  NULL, NULL },
	{ "bug_report",  remmina_main_on_action_application_bug_report, NULL, NULL, NULL},
	{ "dark",	 remmina_main_on_action_application_dark_theme,	 NULL, NULL, NULL },
	{ "debug",	 remmina_main_on_action_help_debug,		 NULL, NULL, NULL },
	{ "community",	 remmina_main_on_action_help_community,		 NULL, NULL, NULL },
	{ "donations",	 remmina_main_on_action_help_donations,		 NULL, NULL, NULL },
	{ "homepage",	 remmina_main_on_action_help_homepage,		 NULL, NULL, NULL },
	{ "wiki",	 remmina_main_on_action_help_wiki,		 NULL, NULL, NULL },
	{ "quit",	 remmina_main_on_action_application_quit,	 NULL, NULL, NULL },
};

static GActionEntry main_actions[] = {
	{ "connect",  remmina_main_on_action_connection_connect,	NULL, NULL, NULL },
	{ "copy",     remmina_main_on_action_connection_copy,		NULL, NULL, NULL },
	{ "delete",   remmina_main_on_action_connection_delete,		NULL, NULL, NULL },
	{ "delete_multiple", remmina_main_on_action_connection_delete_multiple, NULL, NULL, NULL },
	{ "connect_multiple", remmina_main_on_action_connection_connect_multiple, NULL, NULL, NULL },
	{ "edit",     remmina_main_on_action_connection_edit,		NULL, NULL, NULL },
	{ "exttools", remmina_main_on_action_connection_external_tools, NULL, NULL, NULL },
	{ "new",      remmina_main_on_action_connection_new,		NULL, NULL, NULL },
	{ "export",   remmina_main_on_action_tools_export,		NULL, NULL, NULL },
	{ "import",   remmina_main_on_action_tools_import,		NULL, NULL, NULL },
	{ "expand",   remmina_main_on_action_expand,			NULL, NULL, NULL },
	{ "collapse", remmina_main_on_action_collapse,			NULL, NULL, NULL },
	{ "search",   remmina_main_on_action_search_toggle,		NULL, NULL, NULL },
};

static GtkTargetEntry remmina_drop_types[] =
{
	{ "text/uri-list", 0, 1 }
};

static char *quick_connect_plugin_list[] =
{
	"RDP", "VNC", "SSH", "NX", "SPICE", "X2GO"
};

/**
 * Save the Remmina Main Window size to assure the main geometry at each restart
 */
static void remmina_main_save_size(void)
{
	TRACE_CALL(__func__);
	if ((gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(remminamain->window))) & GDK_WINDOW_STATE_MAXIMIZED) == 0) {
		gtk_window_get_size(remminamain->window, &remmina_pref.main_width, &remmina_pref.main_height);
		remmina_pref.main_maximize = FALSE;
	} else {
		remmina_pref.main_maximize = TRUE;
	}
}

static void remmina_main_save_expanded_group_func(GtkTreeView *tree_view, GtkTreePath *path, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkTreeIter iter;
	gchar *group;

	gtk_tree_model_get_iter(remminamain->priv->file_model_sort, &iter, path);
	gtk_tree_model_get(remminamain->priv->file_model_sort, &iter, GROUP_COLUMN, &group, -1);
	if (group) {
		remmina_string_array_add(remminamain->priv->expanded_group, group);
		g_free(group);
	}
}

static void remmina_main_save_expanded_group(void)
{
	TRACE_CALL(__func__);
	if (GTK_IS_TREE_STORE(remminamain->priv->file_model)) {
		if (remminamain->priv->expanded_group)
			remmina_string_array_free(remminamain->priv->expanded_group);
		remminamain->priv->expanded_group = remmina_string_array_new();
		gtk_tree_view_map_expanded_rows(remminamain->tree_files_list,
						(GtkTreeViewMappingFunc)remmina_main_save_expanded_group_func, NULL);
	}
}

/**
 * Save the Remmina Main Window size and the expanded group before to close Remmina.
 * This function uses remmina_main_save_size and remmina_main_save_expanded_group.
 */
void remmina_main_save_before_destroy()
{
	TRACE_CALL(__func__);
	if (!remminamain || !remminamain->window)
		return;

	remmina_main_save_size();
	remmina_main_save_expanded_group();
	g_free(remmina_pref.expanded_group);
	remmina_pref.expanded_group = remmina_string_array_to_string(remminamain->priv->expanded_group);
	remmina_pref_save();
}

void remmina_main_destroy()
{
	TRACE_CALL(__func__);

	if (remminamain) {
		if (remminamain->window)
			gtk_widget_destroy(GTK_WIDGET(remminamain->window));

		g_object_unref(remminamain->builder);
		remmina_string_array_free(remminamain->priv->expanded_group);
		remminamain->priv->expanded_group = NULL;
		if (remminamain->priv->file_model)
			g_object_unref(G_OBJECT(remminamain->priv->file_model));
		g_object_unref(G_OBJECT(remminamain->priv->file_model_filter));
		g_free(remminamain->priv->selected_filename);
		g_free(remminamain->priv->selected_name);
		g_free(remminamain->priv);
		g_free(remminamain);
		remminamain = NULL;
	}
}

/**
 * Try to exit remmina after a delete window event
 */
static gboolean remmina_main_dexit(gpointer data)
{
	TRACE_CALL(__func__);
	remmina_application_condexit(REMMINA_CONDEXIT_ONMAINWINDELETE);
	return FALSE;
}

gboolean remmina_main_on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	TRACE_CALL(__func__);
	remmina_main_save_before_destroy();

	g_idle_add(remmina_main_dexit, NULL);

	return FALSE;
}

gboolean remmina_main_idle_destroy(gpointer data)
{
	TRACE_CALL(__func__);

	if (remminamain)
		remmina_main_destroy();

	return G_SOURCE_REMOVE;
}

/**
 * Called when the remminamain->window widget is destroyed (glade event handler)
 */
void remmina_main_on_destroy_event()
{
	TRACE_CALL(__func__);

	if (remminamain) {
		/* Invalidate remminamain->window to avoid multiple destructions */
		remminamain->window = NULL;
		/* Destroy remminamain struct, later. We can't destroy
		 *      important objects like the builder now */
		g_idle_add(remmina_main_idle_destroy, NULL);
	}
}

static void remmina_main_clear_selection_data(void)
{
	TRACE_CALL(__func__);
	g_free(remminamain->priv->selected_filename);
	g_free(remminamain->priv->selected_name);
	remminamain->priv->selected_filename = NULL;
	remminamain->priv->selected_name = NULL;
}

#ifdef SNAP_BUILD

static void remmina_main_show_snap_welcome()
{
	GtkBuilder *dlgbuilder = NULL;
	GtkWidget *dlg;
	GtkWindow *parent;
	int result;
	static gboolean shown_once = FALSE;
	gboolean need_snap_interface_connections = FALSE;
	GtkWidget *dsa;
	RemminaSecretPlugin *remmina_secret_plugin;

	if (shown_once)
		return;
	else
		shown_once = TRUE;

	g_print("Remmina is compiled as a SNAP package.\n");
	remmina_secret_plugin = remmina_plugin_manager_get_secret_plugin();
	if (remmina_secret_plugin == NULL) {
		g_print("  but we can’t find the secret plugin inside the SNAP.\n");
		need_snap_interface_connections = TRUE;
	} else {
		if (!remmina_secret_plugin->is_service_available(remmina_secret_plugin)) {
			g_print("  but we can’t access a secret service. Secret service or SNAP interface connection is missing.\n");
			need_snap_interface_connections = TRUE;
		}
	}

	if (need_snap_interface_connections && !remmina_pref.prevent_snap_welcome_message) {
		dlgbuilder = remmina_public_gtk_builder_new_from_resource("/org/remmina/Remmina/src/../data/ui/remmina_snap_info_dialog.glade");
		dsa = GTK_WIDGET(gtk_builder_get_object(dlgbuilder, "dontshowagain"));
		if (dlgbuilder) {
			parent = remmina_main_get_window();
			dlg = GTK_WIDGET(gtk_builder_get_object(dlgbuilder, "SnapInfoDlg"));
			if (parent)
				gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);
			gtk_builder_connect_signals(dlgbuilder, NULL);
			result = gtk_dialog_run(GTK_DIALOG(dlg));
			if (result == 1) {
				remmina_pref.prevent_snap_welcome_message = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dsa));
				remmina_pref_save();
			}
			gtk_widget_destroy(dlg);
			g_object_unref(dlgbuilder);
		}
	}
}
#endif


static gboolean remmina_main_selection_func(GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path,
					    gboolean path_currently_selected, gpointer user_data)
{
	TRACE_CALL(__func__);
	guint context_id;
	GtkTreeIter iter;
	gchar buf[1000];

	if (path_currently_selected)
		return TRUE;

	if (!gtk_tree_model_get_iter(model, &iter, path))
		return TRUE;

	remmina_main_clear_selection_data();

	gtk_tree_model_get(model, &iter,
			   NAME_COLUMN, &remminamain->priv->selected_name,
			   FILENAME_COLUMN, &remminamain->priv->selected_filename,
			   -1);

	context_id = gtk_statusbar_get_context_id(remminamain->statusbar_main, "status");
	gtk_statusbar_pop(remminamain->statusbar_main, context_id);
	if (remminamain->priv->selected_filename) {
		g_snprintf(buf, sizeof(buf), "%s (%s)", remminamain->priv->selected_name, remminamain->priv->selected_filename);
		gtk_statusbar_push(remminamain->statusbar_main, context_id, buf);
	} else {
		gtk_statusbar_push(remminamain->statusbar_main, context_id, remminamain->priv->selected_name);
	}

	return TRUE;
}

static void remmina_main_load_file_list_callback(RemminaFile *remminafile, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkTreeIter iter;
	GtkListStore *store;
	gchar* status_icon = "";
	store = GTK_LIST_STORE(user_data);
	gchar *datetime;
	if (remmina_pref_get_boolean("status_check")){
		status_icon = "org.remmina.Remmina-status-grey";
		if (g_hash_table_contains(remminamain->network_states, remminafile->filename)){
			gchar* result = (gchar*)g_hash_table_lookup(remminamain->network_states, remminafile->filename);
			if (result != NULL){
				if (strncmp("Yes", result, strlen("Yes")) == 0){
					status_icon = "org.remmina.Remmina-status-green";
				}
				else if (strncmp("No", result, strlen("No")) == 0){
					status_icon = "org.remmina.Remmina-status-red";
				}
			}
		}
	}
	
	datetime = remmina_file_get_datetime(remminafile);
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
			   PROTOCOL_COLUMN, remmina_file_get_icon_name(remminafile),
			   NAME_COLUMN, remmina_file_get_string(remminafile, "name"),
			   NOTES_COLUMN, g_uri_unescape_string(remmina_file_get_string(remminafile, "notes_text"), NULL),
			   GROUP_COLUMN, remmina_file_get_string(remminafile, "group"),
			   SERVER_COLUMN, remmina_file_get_string(remminafile, "server"),
			   PLUGIN_COLUMN, remmina_file_get_string(remminafile, "protocol"),
			   DATE_COLUMN, datetime,
			   FILENAME_COLUMN, remmina_file_get_filename(remminafile),
			   LABELS_COLUMN, remmina_file_get_string(remminafile, "labels"),
			   STATUS_COLUMN, status_icon,
			   -1);
	g_free(datetime);
}

static gboolean remmina_main_load_file_tree_traverse(GNode *node, GtkTreeStore *store, GtkTreeIter *parent)
{
	TRACE_CALL(__func__);
	GtkTreeIter *iter;
	RemminaGroupData *data;
	GNode *child;

	iter = NULL;
	if (node->data) {
		data = (RemminaGroupData *)node->data;
		iter = g_new0(GtkTreeIter, 1);
		gtk_tree_store_append(store, iter, parent);
		gtk_tree_store_set(store, iter,
				   PROTOCOL_COLUMN, "folder-symbolic",
				   NAME_COLUMN, data->name,
				   GROUP_COLUMN, data->group,
				   DATE_COLUMN, data->datetime,
				   FILENAME_COLUMN, NULL,
				   LABELS_COLUMN, data->labels,
				   -1);
	}
	for (child = g_node_first_child(node); child; child = g_node_next_sibling(child))
		remmina_main_load_file_tree_traverse(child, store, iter);
	g_free(iter);
	return FALSE;
}

static void remmina_main_load_file_tree_group(GtkTreeStore *store)
{
	TRACE_CALL(__func__);
	GNode *root;

	root = remmina_file_manager_get_group_tree();
	remmina_main_load_file_tree_traverse(root, store, NULL);
	remmina_file_manager_free_group_tree(root);
}

static void remmina_main_expand_group_traverse(GtkTreeIter *iter)
{
	TRACE_CALL(__func__);
	GtkTreeModel *tree;
	gboolean ret;
	gchar *group, *filename;
	GtkTreeIter child;
	GtkTreePath *path;

	tree = remminamain->priv->file_model_sort;
	ret = TRUE;
	while (ret) {
		gtk_tree_model_get(tree, iter, GROUP_COLUMN, &group, FILENAME_COLUMN, &filename, -1);
		if (filename == NULL) {
			if (remmina_string_array_find(remminamain->priv->expanded_group, group) >= 0) {
				path = gtk_tree_model_get_path(tree, iter);
				gtk_tree_view_expand_row(remminamain->tree_files_list, path, FALSE);
				gtk_tree_path_free(path);
			}
			if (gtk_tree_model_iter_children(tree, &child, iter))
				remmina_main_expand_group_traverse(&child);
		}
		g_free(group);
		g_free(filename);

		ret = gtk_tree_model_iter_next(tree, iter);
	}
}

static void remmina_main_expand_group(void)
{
	TRACE_CALL(__func__);
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_first(remminamain->priv->file_model_sort, &iter))
		remmina_main_expand_group_traverse(&iter);
}

static gboolean remmina_main_load_file_tree_find(GtkTreeModel *tree, GtkTreeIter *iter, const gchar *match_group)
{
	TRACE_CALL(__func__);
	gboolean ret, match;
	gchar *group, *filename;
	GtkTreeIter child;

	match = FALSE;
	ret = TRUE;
	while (ret) {
		gtk_tree_model_get(tree, iter, GROUP_COLUMN, &group, FILENAME_COLUMN, &filename, -1);
		match = (filename == NULL && g_strcmp0(group, match_group) == 0);
		g_free(group);
		g_free(filename);
		if (match)
			break;
		if (gtk_tree_model_iter_children(tree, &child, iter)) {
			match = remmina_main_load_file_tree_find(tree, &child, match_group);
			if (match) {
				memcpy(iter, &child, sizeof(GtkTreeIter));
				break;
			}
		}
		ret = gtk_tree_model_iter_next(tree, iter);
	}
	return match;
}

static void remmina_main_load_file_tree_callback(RemminaFile *remminafile, gpointer user_data)
{
	TRACE_CALL(__func__);
	GtkTreeIter iter, child;
	GtkTreeStore *store;
	gboolean found;
	gchar *datetime = NULL;
	gchar* status_icon = "";
	if (remmina_pref_get_boolean("status_check")){
		status_icon = "org.remmina.Remmina-status-grey";
		if (g_hash_table_contains(remminamain->network_states, remminafile->filename)){
			gchar* result = (gchar*)g_hash_table_lookup(remminamain->network_states, remminafile->filename);
			if (result != NULL){
				if (strncmp("Yes", result, strlen("Yes")) == 0){
					status_icon = "org.remmina.Remmina-status-green";
				}
				else if (strncmp("No", result, strlen("No")) == 0){
					status_icon = "org.remmina.Remmina-status-red";
				}
			}
		}
	}

	store = GTK_TREE_STORE(user_data);

	found = FALSE;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter))
		found = remmina_main_load_file_tree_find(GTK_TREE_MODEL(store), &iter,
							 remmina_file_get_string(remminafile, "group"));

	datetime = remmina_file_get_datetime(remminafile);
	gtk_tree_store_append(store, &child, (found ? &iter : NULL));
	gtk_tree_store_set(store, &child,
			   PROTOCOL_COLUMN, remmina_file_get_icon_name(remminafile),
			   NAME_COLUMN, remmina_file_get_string(remminafile, "name"),
			   NOTES_COLUMN, g_uri_unescape_string(remmina_file_get_string(remminafile, "notes_text"), NULL),
			   GROUP_COLUMN, remmina_file_get_string(remminafile, "group"),
			   SERVER_COLUMN, remmina_file_get_string(remminafile, "server"),
			   PLUGIN_COLUMN, remmina_file_get_string(remminafile, "protocol"),
			   DATE_COLUMN, datetime,
			   FILENAME_COLUMN, remmina_file_get_filename(remminafile),
			   LABELS_COLUMN, remmina_file_get_string(remminafile, "labels"),
			   STATUS_COLUMN, status_icon,
			   -1);
	g_free(datetime);
}

static void remmina_main_file_model_on_sort(GtkTreeSortable *sortable, gpointer user_data)
{
	TRACE_CALL(__func__);
	gint columnid;
	GtkSortType order;

	gtk_tree_sortable_get_sort_column_id(sortable, &columnid, &order);
	remmina_pref.main_sort_column_id = columnid;
	remmina_pref.main_sort_order = order;
	remmina_pref_save();
}

static gboolean remmina_main_filter_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	TRACE_CALL(__func__);
	gchar *text;
	gchar *protocol, *name, *labels, *group, *server, *plugin, *date, *s;
	gboolean result = TRUE;

	text = g_ascii_strdown(gtk_entry_get_text(remminamain->entry_quick_connect_server), -1);
	if (text && text[0]) {
		gtk_tree_model_get(model, iter,
				   PROTOCOL_COLUMN, &protocol,
				   NAME_COLUMN, &name,
				   GROUP_COLUMN, &group,
				   SERVER_COLUMN, &server,
				   PLUGIN_COLUMN, &plugin,
				   DATE_COLUMN, &date,
				   LABELS_COLUMN, &labels,
				   -1);
		if (g_strcmp0(protocol, "folder-symbolic") != 0) {
			s = g_ascii_strdown(name ? name : "", -1);
			g_free(name);
			name = s;
			s = g_ascii_strdown(group ? group : "", -1);
			g_free(group);
			group = s;
			s = g_ascii_strdown(server ? server : "", -1);
			g_free(server);
			server = s;
			s = g_ascii_strdown(plugin ? plugin : "", -1);
			g_free(plugin);
			plugin = s;
			s = g_ascii_strdown(date ? date : "", -1);
			g_free(date);
			date = s;
			result = (strstr(name, text) || strstr(group, text) || strstr(server, text) || strstr(plugin, text) || strstr(date, text));

			// Filter by labels

			s = g_ascii_strdown(labels ? labels : "", -1);
			g_free(labels);
			labels = s;

			if (strlen(labels) > 0) {
				gboolean labels_result  = TRUE;
				gchar    **labels_array = g_strsplit(labels, ",", -1);
				gchar    **text_array   = g_strsplit(text, ",", -1);

				for (int t = 0; (NULL != text_array[t]); t++) {
					if (0 == strlen(text_array[t])) {
						continue;
					}

					gboolean text_result = FALSE;

					for (int l = 0; (NULL != labels_array[l]); l++) {
						if (0 == strlen(labels_array[l])) {
							continue;
						}

						text_result = (text_result || strstr(labels_array[l], text_array[t]));

						if (text_result) {
							break;
						}
					}

					labels_result = (labels_result && text_result);

					if (!labels_result) {
						break;
					}
				}

				result = (result || labels_result);

				g_strfreev(labels_array);
				g_strfreev(text_array);
			}
		}
		g_free(protocol);
		g_free(name);
		g_free(labels);
		g_free(group);
		g_free(server);
		g_free(plugin);
		g_free(date);
	}
	g_free(text);
	return result;
}

static void remmina_main_select_file(const gchar *filename)
{
	TRACE_CALL(__func__);
	GtkTreeIter iter;
	GtkTreePath *path;
	gchar *item_filename;
	gboolean cmp;

	if (!gtk_tree_model_get_iter_first(remminamain->priv->file_model_sort, &iter))
		return;

	while (TRUE) {
		gtk_tree_model_get(remminamain->priv->file_model_sort, &iter, FILENAME_COLUMN, &item_filename, -1);
		cmp = g_strcmp0(item_filename, filename);
		g_free(item_filename);
		if (cmp == 0) {
			gtk_tree_selection_select_iter(gtk_tree_view_get_selection(remminamain->tree_files_list),
						       &iter);
			path = gtk_tree_model_get_path(remminamain->priv->file_model_sort, &iter);
			gtk_tree_view_scroll_to_cell(remminamain->tree_files_list, path, NULL, TRUE, 0.5, 0.0);
			gtk_tree_path_free(path);
			return;
		}
		if (!gtk_tree_model_iter_next(remminamain->priv->file_model_sort, &iter))
			return;
	}
}

static void remmina_main_load_files()
{
	TRACE_CALL(__func__);
	gint items_count;
	gchar buf[200];
	guint context_id;
	gint view_file_mode;
	gboolean always_show_notes;
	char *save_selected_filename;
	GtkTreeModel *newmodel;
	const gchar *neticon;
	const gchar *connection_tooltip;

	save_selected_filename = g_strdup(remminamain->priv->selected_filename);
	remmina_main_save_expanded_group();

	view_file_mode = remmina_pref.view_file_mode;
	if (remminamain->priv->override_view_file_mode_to_list)
		view_file_mode = REMMINA_VIEW_FILE_LIST;

	switch (remmina_pref.view_file_mode) {
	case REMMINA_VIEW_FILE_TREE:
		gtk_toggle_button_set_active(remminamain->view_toggle_button, FALSE);
		break;
	case REMMINA_VIEW_FILE_LIST:
	default:
		gtk_toggle_button_set_active(remminamain->view_toggle_button, TRUE);
		break;
	}

	switch (view_file_mode) {
	case REMMINA_VIEW_FILE_TREE:
		/* Create new GtkTreeStore model */
		newmodel = GTK_TREE_MODEL(gtk_tree_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING));
		/* Hide the Group column in the tree view mode */
		gtk_tree_view_column_set_visible(remminamain->column_files_list_group, FALSE);
		/* Load groups first */
		remmina_main_load_file_tree_group(GTK_TREE_STORE(newmodel));
		/* Load files list */
		items_count = remmina_file_manager_iterate((GFunc)remmina_main_load_file_tree_callback, (gpointer)newmodel);
		break;

	case REMMINA_VIEW_FILE_LIST:
	default:
		/* Create new GtkListStore model */
		newmodel = GTK_TREE_MODEL(gtk_list_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING));
		/* Show the Group column in the list view mode */
		gtk_tree_view_column_set_visible(remminamain->column_files_list_group, TRUE);
		/* Load files list */
		items_count = remmina_file_manager_iterate((GFunc)remmina_main_load_file_list_callback, (gpointer)newmodel);
		break;
	}

	/* Set note column visibility*/
	always_show_notes = remmina_pref.always_show_notes;
	if (!always_show_notes){
		gtk_tree_view_column_set_visible(remminamain->column_files_list_notes, FALSE);
	}

	/* Unset old model */
	gtk_tree_view_set_model(remminamain->tree_files_list, NULL);

	/* Destroy the old model and save the new one */
	remminamain->priv->file_model = newmodel;

	/* Create a sorted filtered model based on newmodel and apply it to the TreeView */
	remminamain->priv->file_model_filter = gtk_tree_model_filter_new(remminamain->priv->file_model, NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(remminamain->priv->file_model_filter),
					       (GtkTreeModelFilterVisibleFunc)remmina_main_filter_visible_func, NULL, NULL);
	remminamain->priv->file_model_sort = gtk_tree_model_sort_new_with_model(remminamain->priv->file_model_filter);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(remminamain->priv->file_model_sort),
					     remmina_pref.main_sort_column_id,
					     remmina_pref.main_sort_order);
	gtk_tree_view_set_model(remminamain->tree_files_list, remminamain->priv->file_model_sort);
	g_signal_connect(G_OBJECT(remminamain->priv->file_model_sort), "sort-column-changed",
			 G_CALLBACK(remmina_main_file_model_on_sort), NULL);
	remmina_main_expand_group();
	/* Select the file previously selected */
	if (save_selected_filename) {
		remmina_main_select_file(save_selected_filename);
		g_free(save_selected_filename);
	}
	gtk_tree_view_column_set_widget(remminamain->column_files_list_date, NULL);

	GtkWidget *label = gtk_tree_view_column_get_button(remminamain->column_files_list_date);

	gtk_widget_set_tooltip_text(GTK_WIDGET(label),
				    _("The latest successful connection attempt, or a pre-computed date"));
	/* Show in the status bar the total number of connections found */
	g_snprintf(buf, sizeof(buf), ngettext("Total %i item.", "Total %i items.", items_count), items_count);
	context_id = gtk_statusbar_get_context_id(remminamain->statusbar_main, "status");
	gtk_statusbar_pop(remminamain->statusbar_main, context_id);
	gtk_statusbar_push(remminamain->statusbar_main, context_id, buf);

	remmina_network_monitor_status (remminamain->monitor);
	if (remminamain->monitor->connected){
		neticon = g_strdup("network-transmit-receive-symbolic");
		connection_tooltip = g_strdup(_("Network status: fully online"));
	} else {
		neticon = g_strdup("network-offline-symbolic");
		connection_tooltip = g_strdup(_("Network status: offline"));
	}

	if (GTK_IS_WIDGET(remminamain->network_icon))
		gtk_widget_destroy(remminamain->network_icon);
	GIcon *icon = g_themed_icon_new (neticon);
	remminamain->network_icon = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text (remminamain->network_icon, connection_tooltip);

	g_object_unref (icon);

	gtk_box_pack_start (GTK_BOX(remminamain->statusbar_main), remminamain->network_icon, FALSE, FALSE, 0);
	gtk_widget_show (remminamain->network_icon);

}

void remmina_main_load_files_cb(GtkEntry *entry, char *string, gpointer user_data)
{
	TRACE_CALL(__func__);
	remmina_main_load_files();
}


static void remmina_main_load_by_group_callback(RemminaFile *remminafile, gpointer user_data)
{
	
	const gchar* group = remmina_file_get_string(remminafile, "group");

	if (g_strcmp0(remminamain->priv->selected_name, group) == 0 ){
		if (remmina_pref_get_boolean("use_primary_password")
			&& remmina_pref_get_boolean("lock_connect")
			&& remmina_unlock_new(remminamain->window) == 0)
			return;
		if (remmina_file_get_int (remminafile, "profile-lock", FALSE) == 1
				&& remmina_unlock_new(remminamain->window) == 0)
			return;

		remmina_file_touch(remminafile);
		rcw_open_from_filename(remminafile->filename);
	}
}

void remmina_main_on_action_connection_connect(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);

	RemminaFile *remminafile;

	if (!remminamain->priv->selected_filename){
		if (remminamain->priv->selected_name){
			remmina_file_manager_iterate((GFunc)remmina_main_load_by_group_callback, NULL);
		}
		return;
	}

	remminafile = remmina_file_load(remminamain->priv->selected_filename);

	if (remminafile == NULL)
		return;

	if (remmina_pref_get_boolean("use_primary_password")
			&& remmina_pref_get_boolean("lock_connect")
			&& remmina_unlock_new(remminamain->window) == 0)
		return;
	if (remmina_file_get_int (remminafile, "profile-lock", FALSE) == 1
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	remmina_file_touch(remminafile);
	rcw_open_from_filename(remminamain->priv->selected_filename);

	remmina_file_free(remminafile);
}

void remmina_main_on_action_connection_external_tools(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	if (!remminamain->priv->selected_filename)
		return;

	remmina_external_tools_from_filename(remminamain, remminamain->priv->selected_filename);
}

static void remmina_main_file_editor_destroy(GtkWidget *widget, gpointer user_data)
{
	TRACE_CALL(__func__);

	if (!remminamain)
		return;
	remmina_main_load_files();
}

void remmina_main_on_action_application_mpchange(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaFile *remminafile;

	const gchar *username;
	const gchar *domain;
	const gchar *group;
	const gchar *gatewayusername;
	const gchar *gatewaydomain;

	username = domain = group = gatewayusername = gatewaydomain = "";

	remminafile = NULL;

	if (remmina_pref_get_boolean("use_primary_password")
			&& remmina_pref_get_boolean("lock_edit")
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	if (remminamain->priv->selected_filename) {
		remminafile = remmina_file_load(remminamain->priv->selected_filename);
		if (remminafile != NULL) {
			username = remmina_file_get_string(remminafile, "username");
			domain = remmina_file_get_string(remminafile, "domain");
			group = remmina_file_get_string(remminafile, "group");
			gatewayusername = remmina_file_get_string(remminafile, "gateway_username");
			gatewaydomain = remmina_file_get_string(remminafile, "gateway_domain");
		}
	}

	remmina_mpchange_schedule(TRUE, group, domain, username, "", gatewayusername, gatewaydomain, "");

	if (remminafile != NULL)
		remmina_file_free(remminafile);
}

void remmina_main_on_action_connection_new(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	if (kioskmode && kioskmode == TRUE)
		return;
	GtkWidget *widget;

	remmina_plugin_manager_get_available_plugins();
	if (remmina_pref_get_boolean("use_primary_password")
			&& remmina_pref_get_boolean("lock_edit")
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	widget = remmina_file_editor_new();
	g_signal_connect(G_OBJECT(widget), "destroy", G_CALLBACK(remmina_main_file_editor_destroy), remminamain);
	gtk_window_set_transient_for(GTK_WINDOW(widget), remminamain->window);
	gtk_widget_show(widget);
	remmina_main_load_files();
}

static gboolean remmina_main_search_key_event(GtkWidget *search_entry, GdkEventKey *event, gpointer user_data)
{
	TRACE_CALL(__func__);
	if (event->keyval == GDK_KEY_Escape) {
		gtk_entry_set_text(remminamain->entry_quick_connect_server, "");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(RM_GET_OBJECT("search_toggle")), FALSE);
		return TRUE;
	}
	return FALSE;
}

static gboolean remmina_main_tree_row_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
	TRACE_CALL(__func__);
	if (gtk_tree_view_row_expanded(tree, path))
		gtk_tree_view_collapse_row(tree, path);
	else
		gtk_tree_view_expand_row(tree, path, FALSE);
	return TRUE;
}

void remmina_main_on_view_toggle()
{
	if (gtk_toggle_button_get_active(remminamain->view_toggle_button)) {
		if (remmina_pref.view_file_mode != REMMINA_VIEW_FILE_LIST) {
			remmina_pref.view_file_mode = REMMINA_VIEW_FILE_LIST;
			gtk_entry_set_text(remminamain->entry_quick_connect_server, "");
			remmina_pref_save();
			remmina_main_load_files();
		}
	} else {
		if (remmina_pref.view_file_mode != REMMINA_VIEW_FILE_TREE) {
			remmina_pref.view_file_mode = REMMINA_VIEW_FILE_TREE;
			gtk_entry_set_text(remminamain->entry_quick_connect_server, "");
			remmina_pref_save();
			remmina_main_load_files();
		}
	}
}

void remmina_main_on_action_connection_copy(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkWidget *widget;

	if (remmina_pref_get_boolean("use_primary_password")
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	if (!remminamain->priv->selected_filename)
		return;

	RemminaFile *remminafile = remmina_file_load(remminamain->priv->selected_filename);

	if (((remmina_pref_get_boolean("lock_edit")
				&& remmina_pref_get_boolean("use_primary_password"))
				|| remmina_file_get_int (remminafile, "profile-lock", FALSE))
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	if (remminafile) {
		remmina_file_free(remminafile);
		remminafile = NULL;
	}

	widget = remmina_file_editor_new_copy(remminamain->priv->selected_filename);
	if (widget) {
		g_signal_connect(G_OBJECT(widget), "destroy", G_CALLBACK(remmina_main_file_editor_destroy), remminamain);
		gtk_window_set_transient_for(GTK_WINDOW(widget), remminamain->window);
		gtk_widget_show(widget);
	}
	/* Select the file previously selected */
	if (remminamain->priv->selected_filename)
		remmina_main_select_file(remminamain->priv->selected_filename);
}

void remmina_main_on_action_connection_edit(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkWidget *widget;

	if (!remminamain->priv->selected_filename)
		return;

	RemminaFile *remminafile = remmina_file_load(remminamain->priv->selected_filename);

	if (remmina_pref_get_boolean("use_primary_password")
			&& (remmina_pref_get_boolean("lock_edit")
				|| remmina_file_get_int (remminafile, "profile-lock", FALSE))
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	if (remminafile) {
		remmina_file_free(remminafile);
		remminafile = NULL;
	}

	widget = remmina_file_editor_new_from_filename(remminamain->priv->selected_filename);
	if (widget) {
		gtk_window_set_transient_for(GTK_WINDOW(widget), remminamain->window);
		gtk_widget_show(widget);
	}
/* Select the file previously selected */
	if (remminamain->priv->selected_filename)
		remmina_main_select_file(remminamain->priv->selected_filename);
}

void remmina_main_on_action_connection_delete(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkWidget *dialog;

	if (!remminamain->priv->selected_filename)
		return;

	RemminaFile *remminafile = remmina_file_load(remminamain->priv->selected_filename);

	if (((remmina_pref_get_boolean("lock_edit")
				&& remmina_pref_get_boolean("use_primary_password"))
				|| remmina_file_get_int (remminafile, "profile-lock", FALSE))
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	if (remminafile) {
		remmina_file_free(remminafile);
		remminafile = NULL;
	}

	dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
					_("Are you sure you want to delete “%s”?"), remminamain->priv->selected_name);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
		gchar *delfilename = g_strdup(remminamain->priv->selected_filename);
		remmina_file_delete(delfilename);
		g_free(delfilename), delfilename = NULL;
		remmina_icon_populate_menu();
		remmina_main_load_files();
	}
	gtk_widget_destroy(dialog);
	remmina_main_clear_selection_data();
}


void remmina_main_on_action_connection_connect_multiple(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkTreeSelection *sel = gtk_tree_view_get_selection(remminamain->tree_files_list);
	GtkTreeModel *model = gtk_tree_view_get_model(remminamain->tree_files_list);
	GList *list = gtk_tree_selection_get_selected_rows(sel, &model);
	gchar *file_to_load;


	while (list) {
		GtkTreePath *path = list->data;
		GtkTreeIter iter;
		
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			GtkWidget *dialog_warning;
			dialog_warning = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, 
				_("Failed to load files!"));
			gtk_dialog_run(GTK_DIALOG(dialog_warning));
			gtk_widget_destroy(dialog_warning);
			remmina_main_clear_selection_data();
			return;
		}

		gtk_tree_model_get(model, &iter, 
				FILENAME_COLUMN, &file_to_load, -1);

		RemminaFile *remminafile = remmina_file_load(file_to_load);

		if (remminafile == NULL)
			return;

		if (((remmina_pref_get_boolean("lock_edit")
				&& remmina_pref_get_boolean("use_primary_password"))
				|| remmina_file_get_int (remminafile, "profile-lock", FALSE))
			&& remmina_unlock_new(remminamain->window) == 0)
			return;



		if (remmina_file_get_int (remminafile, "profile-lock", FALSE) == 1
			&& remmina_unlock_new(remminamain->window) == 0)
				return;

		remmina_file_touch(remminafile);
		rcw_open_from_filename(file_to_load);

		

		if (remminafile) {
			remmina_file_free(remminafile);
			remminafile = NULL;
		}

		list = g_list_next(list);
	}
	
	remmina_main_clear_selection_data();
}



void remmina_main_on_action_connection_delete_multiple(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkWidget *dialog;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(remminamain->tree_files_list);
	GtkTreeModel *model = gtk_tree_view_get_model(remminamain->tree_files_list);
	GList *list = gtk_tree_selection_get_selected_rows(sel, &model);
	gchar *file_to_delete;

	dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("Are you sure you want to delete the selected files?"));

	// Delete files if Yes is clicked
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
		while (list) {
			GtkTreePath *path = list->data;
			GtkTreeIter iter;
			
			if (!gtk_tree_model_get_iter(model, &iter, path)) {
				GtkWidget *dialog_warning;
				dialog_warning = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, 
					_("Failed to delete files!"));
				gtk_dialog_run(GTK_DIALOG(dialog_warning));
				gtk_widget_destroy(dialog_warning);
				gtk_widget_destroy(dialog);
				remmina_main_clear_selection_data();
				return;
			}

			gtk_tree_model_get(model, &iter, 
					FILENAME_COLUMN, &file_to_delete, -1);

			RemminaFile *remminafile = remmina_file_load(file_to_delete);

			if (((remmina_pref_get_boolean("lock_edit")
					&& remmina_pref_get_boolean("use_primary_password"))
					|| remmina_file_get_int (remminafile, "profile-lock", FALSE))
				&& remmina_unlock_new(remminamain->window) == 0)
				return;

			if (remminafile) {
				remmina_file_free(remminafile);
				remminafile = NULL;
			}

			gchar *delfilename = g_strdup(file_to_delete);
			remmina_file_delete(delfilename);
			g_free(delfilename), delfilename = NULL;
			remmina_icon_populate_menu();
			remmina_main_load_files();
			list = g_list_next(list);
		}
	}
	
	gtk_widget_destroy(dialog);
	remmina_main_clear_selection_data();
}

void remmina_main_on_accel_application_preferences(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GVariant *v = g_variant_new("i", 0);

	remmina_main_on_action_application_preferences(NULL, v, NULL);
}

void remmina_main_reload_preferences()
{
	GtkSettings *settings;
	settings = gtk_settings_get_default();
	g_object_set(settings, "gtk-application-prefer-dark-theme", remmina_pref.dark_theme, NULL);
	if (remminamain) {
		if(remmina_pref.hide_searchbar){
			gtk_toggle_button_set_active(remminamain->search_toggle, FALSE);
		}
		else{
			gtk_toggle_button_set_active(remminamain->search_toggle, TRUE);
		}
		gtk_tree_view_column_set_visible(remminamain->column_files_list_notes, remmina_pref.always_show_notes);
	}
}

void remmina_main_on_action_application_preferences(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);

	REMMINA_DEBUG("Opening the preferences");
	gint32 tab_num;

	if (param) {
		REMMINA_DEBUG("Parameter passed to preferences of type %s", g_variant_get_type_string(param));
		tab_num = g_variant_get_int32(param);
		REMMINA_DEBUG("We got a parameter for the preferences: %d", tab_num);
	} else {
		tab_num = 0;
	}

	if (remmina_pref_get_boolean("use_primary_password")
			&& remmina_unlock_new(remminamain->window) == 0)
		return;

	GtkWidget *widget = remmina_pref_dialog_new(tab_num, remminamain->window);

	gtk_widget_show(widget);	
}

void remmina_main_on_action_application_default(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
#ifndef __APPLE__
	g_autoptr(GError) error = NULL;
	GDesktopAppInfo *desktop_info;
	GAppInfo *info = NULL;
	g_autofree gchar *id = g_strconcat(REMMINA_APP_ID, ".desktop", NULL);
	int i;

	desktop_info = g_desktop_app_info_new(id);
	if (!desktop_info)
		return;

	info = G_APP_INFO(desktop_info);

	for (i = 0; supported_mime_types[i]; i++) {
		if (!g_app_info_set_as_default_for_type(info, supported_mime_types[i], &error))
			g_warning("Failed to set '%s' as the default application for secondary content type '%s': %s",
				  g_app_info_get_name(info), supported_mime_types[i], error->message);
		else
			g_debug("Set '%s' as the default application for '%s'",
				g_app_info_get_name(info),
				supported_mime_types[i]);
	}
#endif
}

void remmina_main_on_action_application_quit(GSimpleAction *action, GVariant *param, gpointer data)
{
	// Called by quit signal in remmina_main.glade
	TRACE_CALL(__func__);
	g_debug("Quit intercept");
	remmina_application_condexit(REMMINA_CONDEXIT_ONQUIT);
}

void remmina_main_on_date_column_sort_clicked()
{
	if (remmina_pref.view_file_mode != REMMINA_VIEW_FILE_LIST) {
		remmina_pref.view_file_mode = REMMINA_VIEW_FILE_LIST;
		gtk_entry_set_text(remminamain->entry_quick_connect_server, "");
		remmina_pref_save();
		remmina_main_load_files();
	}
}

void remmina_main_toggle_password_view(GtkWidget *widget, gpointer data)
{
	GtkWindow *mainwindow;
	gboolean visible = gtk_entry_get_visibility(GTK_ENTRY(widget));

	mainwindow = remmina_main_get_window();
	if (remmina_pref_get_boolean("use_primary_password") && remmina_pref_get_boolean("lock_view_passwords") && remmina_unlock_new(mainwindow) == 0)
		return;

	if (visible) {
		gtk_entry_set_visibility(GTK_ENTRY(widget), FALSE);
		gtk_entry_set_icon_from_icon_name(GTK_ENTRY(widget), GTK_ENTRY_ICON_SECONDARY, "org.remmina.Remmina-password-reveal-symbolic");
	} else {
		gtk_entry_set_visibility(GTK_ENTRY(widget), TRUE);
		gtk_entry_set_icon_from_icon_name(GTK_ENTRY(widget), GTK_ENTRY_ICON_SECONDARY, "org.remmina.Remmina-password-conceal-symbolic");
	}
}

static void remmina_main_import_file_list(GSList *files)
{
	TRACE_CALL(__func__);
	GtkWidget *dlg;
	GSList *element;
	gchar *path;
	RemminaFilePlugin *plugin;
	GString *err;
	RemminaFile *remminafile = NULL;
	gboolean imported;

	err = g_string_new(NULL);
	imported = FALSE;
	for (element = files; element; element = element->next) {
		path = (gchar *)element->data;
		plugin = remmina_plugin_manager_get_import_file_handler(path);
		if (plugin && (remminafile = plugin->import_func(plugin, path)) != NULL && remmina_file_get_string(remminafile, "name")) {
			remmina_file_generate_filename(remminafile);
			remmina_file_save(remminafile);
			imported = TRUE;
		} else {
			g_string_append(err, path);
			g_string_append_c(err, '\n');
		}
		if (remminafile) {
			remmina_file_free(remminafile);
			remminafile = NULL;
		}
		g_free(path);
	}
	g_slist_free(files);
	if (err->len > 0) {
		// TRANSLATORS: The placeholder %s is an error message
		dlg = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					     _("Unable to import:\n%s"), err->str);
		g_signal_connect(G_OBJECT(dlg), "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show(dlg);
	}
	g_string_free(err, TRUE);
	if (imported)
		remmina_main_load_files();
}

static void remmina_main_action_tools_import_on_response(GtkNativeDialog *dialog, gint response_id, gpointer user_data)
{
	TRACE_CALL(__func__);
	GSList *files;

	if (response_id == GTK_RESPONSE_ACCEPT) {
		files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
		remmina_main_import_file_list(files);
	}
	gtk_native_dialog_destroy(dialog);
}

static void remmina_set_file_chooser_filters(GtkFileChooser *chooser)
{
	GtkFileFilter *filter;

	g_return_if_fail(GTK_IS_FILE_CHOOSER(chooser));

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("RDP Files"));
	gtk_file_filter_add_pattern(filter, "*.rdp");
	gtk_file_filter_add_pattern(filter, "*.rdpx");
	gtk_file_filter_add_pattern(filter, "*.RDP");
	gtk_file_filter_add_pattern(filter, "*.RDPX");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), filter);

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("Virt-Viewer Files"));
	gtk_file_filter_add_pattern(filter, "*.vv");
	gtk_file_chooser_add_filter(chooser, filter);

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("All Files"));
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(chooser, filter);
}

void remmina_main_on_action_tools_import(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkFileChooserNative *chooser;

	chooser = gtk_file_chooser_native_new(_("Import"), remminamain->window,
					      GTK_FILE_CHOOSER_ACTION_OPEN, _("Import"), _("_Cancel"));
	gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(chooser), TRUE);
	remmina_set_file_chooser_filters(GTK_FILE_CHOOSER(chooser));
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);
	g_signal_connect(chooser, "response", G_CALLBACK(remmina_main_action_tools_import_on_response), NULL);
	gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

static void on_export_save_response (GtkFileChooserNative *dialog, int response, RemminaFile *remminafile)
{
	if (response == GTK_RESPONSE_ACCEPT) {
		RemminaFilePlugin *plugin = remmina_plugin_manager_get_export_file_handler(remminafile);
		if (plugin){
			gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
			plugin->export_func(plugin, remminafile, path);
			g_free(path);
		}
	}
	remmina_file_free(remminafile);
	gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(dialog));
}

void remmina_main_on_action_tools_export(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	RemminaFilePlugin *plugin;
	RemminaFile *remminafile;
	GtkWidget *dialog;
	GtkFileChooserNative *chooser;
	gchar *export_name;

	if (!remminamain->priv->selected_filename) {
		dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						_("Select the connection profile."));
		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show(dialog);
		return;
	}

	remminafile = remmina_file_load(remminamain->priv->selected_filename);
	if (remminafile == NULL) {
		dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						_("Remmina couldn't export."));
		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show(dialog);
		return;
	}

	plugin = remmina_plugin_manager_get_export_file_handler(remminafile);
	if (plugin) {
		chooser = gtk_file_chooser_native_new(plugin->export_hints, remminamain->window,
						      GTK_FILE_CHOOSER_ACTION_SAVE, _("_Save"), _("_Cancel"));
		gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(chooser), TRUE);
		remmina_set_file_chooser_filters(GTK_FILE_CHOOSER(chooser));
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
		export_name = g_strdup_printf("%s%s", remminamain->priv->selected_name, plugin->export_ext);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), export_name);
		g_free(export_name);
		g_signal_connect(chooser, "response", G_CALLBACK(on_export_save_response), remminafile);
		gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
	} else
	{
		remmina_file_free(remminafile);
		dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						_("This protocol does not support exporting."));
		g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show(dialog);
		return;
	}
}

void remmina_main_on_action_application_plugins(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	remmina_plugin_manager_get_available_plugins();
	remmina_plugin_manager_show(remminamain->window);
}

void remmina_main_on_action_application_dark_theme(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	GtkSettings *settings;

	settings = gtk_settings_get_default();

	if (gtk_switch_get_active(remminamain->switch_dark_mode))
		remmina_pref.dark_theme = 1;
	else
		remmina_pref.dark_theme = 0;
	remmina_pref_save();

	g_object_set(settings, "gtk-application-prefer-dark-theme", remmina_pref.dark_theme, NULL);
}

void remmina_main_on_action_help_homepage(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	g_app_info_launch_default_for_uri("https://www.remmina.org", NULL, NULL);
}

void remmina_main_on_action_help_wiki(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	g_app_info_launch_default_for_uri("https://gitlab.com/Remmina/Remmina/wikis/home", NULL, NULL);
}

void remmina_main_on_action_help_community(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	g_app_info_launch_default_for_uri("https://remmina.org/community", NULL, NULL);
}

void remmina_main_on_action_help_donations(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	g_app_info_launch_default_for_uri("https://www.remmina.org/donations", NULL, NULL);
}

void remmina_main_on_action_help_debug(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	remmina_log_start();
}

void remmina_main_on_action_application_about(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	remmina_about_open(remminamain->window);
};

void remmina_main_on_action_application_bug_report(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	remmina_bug_report_open(remminamain->window);
};

static gboolean is_empty(const gchar *s)
{
	if (s == NULL)
		return TRUE;
	while (*s != 0) {
		if (!isspace((unsigned char)*s))
			return FALSE;
		s++;
	}
	return TRUE;
}

static gboolean remmina_main_quickconnect(void)
{
	TRACE_CALL(__func__);
	RemminaFile *remminafile;
	gchar *server;
	gchar *server_trimmed;
	gchar *qcp;


	/* Save quick connect protocol if different from the previous one */
	qcp = gtk_combo_box_text_get_active_text(remminamain->combo_quick_connect_protocol);
	if (qcp && strcmp(qcp, remmina_pref.last_quickconnect_protocol) != 0) {
		g_free(remmina_pref.last_quickconnect_protocol);
		remmina_pref.last_quickconnect_protocol = g_strdup(qcp);
		remmina_pref_save();
	}

	remminafile = remmina_file_new();
	server = g_strdup(gtk_entry_get_text(remminamain->entry_quick_connect_server));
	if (g_hostname_to_ascii(server) == NULL) {
		g_free(server), server = NULL;
		return FALSE;
	}
	/* If server contain /, e.g. vnc://, it won't connect
	 * We could search for an array of invalid characters, but
	 * it's better to find a way to correctly parse and validate addresses
	 */
	if (g_strrstr(server, "/") != NULL) {
		g_free(server), server = NULL;
		return FALSE;
	}
	if (is_empty(server)){
		g_free(server), server = NULL;
		return FALSE;
	}

	/* check if server is an IP address and trim whitespace if so */
	server_trimmed = g_strdup(server);
	g_strstrip(server_trimmed);
	gchar **strings = g_strsplit(server_trimmed, ":", 2);

	if (strings[0] != NULL)
		if (g_hostname_is_ip_address(strings[0]))
			g_stpcpy(server, server_trimmed);

	remmina_file_set_string(remminafile, "sound", "off");
	remmina_file_set_string(remminafile, "server", server);
	remmina_file_set_string(remminafile, "name", server);
	remmina_file_set_string(remminafile, "protocol", qcp);
	g_free(server);
	g_free(server_trimmed);
	g_free(qcp);

	rcw_open_from_file(remminafile);

	return FALSE;
}

gboolean remmina_main_quickconnect_on_click(GtkWidget *widget, gpointer user_data)
{
	TRACE_CALL(__func__);
	if (!kioskmode && kioskmode == FALSE)
		return remmina_main_quickconnect();
	return FALSE;
}

/* Select all the text inside the quick search box if there is anything */
void remmina_main_quick_search_enter(GtkWidget *widget, gpointer user_data)
{
	if (gtk_entry_get_text(remminamain->entry_quick_connect_server))
		gtk_editable_select_region(GTK_EDITABLE(remminamain->entry_quick_connect_server), 0, -1);
}

void remmina_main_on_action_collapse(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	gtk_tree_view_collapse_all(remminamain->tree_files_list);
}

void remmina_main_on_action_search_toggle(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	REMMINA_DEBUG("Search toggle triggered");

	gboolean toggle_status = gtk_toggle_button_get_active(remminamain->search_toggle);

	gtk_search_bar_set_search_mode(remminamain->search_bar, toggle_status);
	if (toggle_status) {
		REMMINA_DEBUG("Search toggle is active");
		gtk_widget_grab_focus(GTK_WIDGET(remminamain->entry_quick_connect_server));
	} else {
		REMMINA_DEBUG("Search toggle is not active, focus is tree_files_list");
		gtk_widget_grab_focus(GTK_WIDGET(remminamain->tree_files_list));
	}
}

void remmina_main_on_accel_search_toggle(RemminaMain *remminamain)
{
	TRACE_CALL(__func__);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remminamain->search_toggle), TRUE);
}

void remmina_main_on_action_expand(GSimpleAction *action, GVariant *param, gpointer data)
{
	TRACE_CALL(__func__);
	gtk_tree_view_expand_all(remminamain->tree_files_list);
}

/* Handle double click on a row in the connections list */
void remmina_main_file_list_on_row_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
	TRACE_CALL(__func__);
/* If a connection was selected then execute the default action */
	if (remminamain->priv->selected_filename) {
		switch (remmina_pref.default_action) {
		case REMMINA_ACTION_EDIT:
			remmina_main_on_action_connection_edit(NULL, NULL, NULL);
			break;
		case REMMINA_ACTION_CONNECT:
		default:
			remmina_main_on_action_connection_connect(NULL, NULL, NULL);
			break;
		}
	}
}

/* Show the popup menu by the right button mouse click */
gboolean remmina_main_file_list_on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	TRACE_CALL(__func__);
	if (event->button == MOUSE_BUTTON_RIGHT) {
		if (!kioskmode && kioskmode == FALSE) {
#if GTK_CHECK_VERSION(3, 22, 0)
			// For now, if more than one selected row, display only a delete menu option
			if (gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(remminamain->tree_files_list)) > 1) {
				gtk_menu_popup_at_pointer(GTK_MENU(remminamain->menu_popup_multi), (GdkEvent *)event);
				return GDK_EVENT_STOP;
			}
			else {
				gtk_menu_popup_at_pointer(GTK_MENU(remminamain->menu_popup), (GdkEvent *)event);
			}
#else
			gtk_menu_popup(remminamain->menu_popup, NULL, NULL, NULL, NULL, event->button, event->time);
#endif
		}
	}
	return FALSE;
}

/* Show the popup menu by the menu key */
gboolean remmina_main_file_list_on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	TRACE_CALL(__func__);
	if (event->keyval == GDK_KEY_Menu) {
#if GTK_CHECK_VERSION(3, 22, 0)
		gtk_menu_popup_at_widget(GTK_MENU(remminamain->menu_popup), widget,
					 GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
					 (GdkEvent *)event);
#else
		gtk_menu_popup(remminamain->menu_popup, NULL, NULL, NULL, NULL, 0, event->time);
#endif
	}
	return FALSE;
}

void remmina_main_quick_search_on_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer user_data)
{
	TRACE_CALL(__func__);
	if (icon_pos == GTK_ENTRY_ICON_SECONDARY)
		gtk_entry_set_text(entry, "");
}

void remmina_main_quick_search_on_changed(GtkEditable *editable, gpointer user_data)
{
	TRACE_CALL(__func__);
	/* If a search text was input then temporary set the file mode to list */
	if (gtk_entry_get_text_length(remminamain->entry_quick_connect_server)) {
		if (GTK_IS_TREE_STORE(remminamain->priv->file_model)) {
			/* File view mode changed, put it to override and reload list */
			remminamain->priv->override_view_file_mode_to_list = TRUE;
			remmina_main_load_files();
		}
	} else {
		if (remminamain->priv->override_view_file_mode_to_list) {
			/* File view mode changed, put it to default (disable override) and reload list */
			remminamain->priv->override_view_file_mode_to_list = FALSE;
			remmina_main_load_files();
		}
	}
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(remminamain->priv->file_model_filter));
}

void remmina_main_on_drag_data_received(GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y,
					GtkSelectionData *data, guint info, guint time, gpointer user_data)
{
	TRACE_CALL(__func__);
	gchar **uris;
	GSList *files = NULL;
	gint i;

	uris = g_uri_list_extract_uris((const gchar *)gtk_selection_data_get_data(data));
	for (i = 0; uris[i]; i++) {
		if (strncmp(uris[i], "file://", 7) != 0)
			continue;
		files = g_slist_append(files, g_strdup(uris[i] + 7));
	}
	g_strfreev(uris);
	remmina_main_import_file_list(files);
}

/* Add a new menuitem to the Tools menu */
static gboolean remmina_main_add_tool_plugin(gchar *name, RemminaPlugin *plugin, gpointer user_data)
{
	TRACE_CALL(__func__);
	RemminaToolPlugin *tool_plugin = (RemminaToolPlugin *)plugin;
	GtkWidget *menuitem = gtk_menu_item_new_with_label(plugin->description);

	gtk_widget_show(menuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(remminamain->menu_popup_full), menuitem);
	g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(tool_plugin->exec_func), tool_plugin);
	return FALSE;
}

gboolean remmina_main_on_window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
	TRACE_CALL(__func__);
	return FALSE;
}

/* Remmina main window initialization */
static void remmina_main_init(void)
{
	TRACE_CALL(__func__);
	int i, qcp_idx, qcp_actidx;
	char *name;
	GtkSettings *settings;

	REMMINA_DEBUG("Initializing the Remmina main window");
	/* Switch to a dark theme if the user enabled it */
	settings = gtk_settings_get_default();
	g_object_set(settings, "gtk-application-prefer-dark-theme", remmina_pref.dark_theme, NULL);

	REMMINA_DEBUG ("Initializing monitor");
	remminamain->monitor = remmina_network_monitor_new();

	remminamain->priv->expanded_group = remmina_string_array_new_from_string(remmina_pref.expanded_group);
	if (!kioskmode && kioskmode == FALSE)
		gtk_window_set_title(remminamain->window, _("Remmina Remote Desktop Client"));
	else
		gtk_window_set_title(remminamain->window, _("Remmina Kiosk"));
	if (!kioskmode && kioskmode == FALSE) {
		gtk_window_set_default_size(remminamain->window, remmina_pref.main_width, remmina_pref.main_height);
		if (remmina_pref.main_maximize)
			gtk_window_maximize(remminamain->window);
	}
	/* Honor global preferences Search Bar visibility */
	if (remmina_pref.hide_searchbar)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(RM_GET_OBJECT("search_toggle")), FALSE);

	/* Add a GtkMenuItem to the Tools menu for each plugin of type REMMINA_PLUGIN_TYPE_TOOL */
	remmina_plugin_manager_for_each_plugin(REMMINA_PLUGIN_TYPE_TOOL, remmina_main_add_tool_plugin, remminamain);

	/* Add available quick connect protocols to remminamain->combo_quick_connect_protocol */
	qcp_idx = qcp_actidx = 0;
	for (i = 0; i < sizeof(quick_connect_plugin_list) / sizeof(quick_connect_plugin_list[0]); i++) {
		name = quick_connect_plugin_list[i];
		if (remmina_plugin_manager_get_plugin(REMMINA_PLUGIN_TYPE_PROTOCOL, name)) {
			gtk_combo_box_text_append(remminamain->combo_quick_connect_protocol, name, name);
			if (remmina_pref.last_quickconnect_protocol != NULL && strcmp(name, remmina_pref.last_quickconnect_protocol) == 0)
				qcp_actidx = qcp_idx;
			qcp_idx++;
		}
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(remminamain->combo_quick_connect_protocol), qcp_actidx);

	/* Set the Quick Connection */
	gtk_entry_set_activates_default(remminamain->entry_quick_connect_server, TRUE);
	/* Set the TreeView for the files list */
	gtk_tree_selection_set_select_function(
		gtk_tree_view_get_selection(remminamain->tree_files_list),
		remmina_main_selection_func, NULL, NULL);
	/** @todo Set entry_quick_connect_server as default search entry. Weirdly. This does not work yet. */
	gtk_tree_view_set_search_entry(remminamain->tree_files_list, GTK_ENTRY(remminamain->entry_quick_connect_server));
	if (remmina_pref.hide_searchbar)
		gtk_widget_grab_focus(GTK_WIDGET(remminamain->tree_files_list));
	/* Load the files list */
	remmina_main_load_files();

	/* Drag-n-drop support */
	gtk_drag_dest_set(GTK_WIDGET(remminamain->window), GTK_DEST_DEFAULT_ALL, remmina_drop_types, 1, GDK_ACTION_COPY);

	/* Finish initialization */
	remminamain->priv->initialized = TRUE;

	/* Register the window in remmina_widget_pool with GType=GTK_WINDOW and TAG=remmina-main-window */
	g_object_set_data(G_OBJECT(remminamain->window), "tag", "remmina-main-window");
	remmina_widget_pool_register(GTK_WIDGET(remminamain->window));
}

/* Signal handler for "show" on remminamain->window */
void remmina_main_on_show(GtkWidget *w, gpointer user_data)
{
	TRACE_CALL(__func__);
#ifdef SNAP_BUILD
	remmina_main_show_snap_welcome();
#endif
}

void remmina_main_add_network_status(gchar* key, gchar* value)
{
	if (remminamain != NULL){
		g_hash_table_insert(remminamain->network_states, key, value);
		remmina_main_load_files();
	}
	
}

/* RemminaMain instance */
GtkWidget *remmina_main_new(void)
{
	TRACE_CALL(__func__);
	GSimpleActionGroup *actions;
	GtkAccelGroup *accel_group = NULL;

	remminamain = g_new0(RemminaMain, 1);
	remminamain->priv = g_new0(RemminaMainPriv, 1);
	remminamain->network_states = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	/* Assign UI widgets to the private members */
	remminamain->builder = remmina_public_gtk_builder_new_from_resource("/org/remmina/Remmina/src/../data/ui/remmina_main.glade");
	remminamain->window = GTK_WINDOW(RM_GET_OBJECT("RemminaMain"));
	if (kioskmode && kioskmode == TRUE) {
		gtk_window_set_position(remminamain->window, GTK_WIN_POS_CENTER_ALWAYS);
		gtk_window_set_default_size(remminamain->window, 800, 400);
		gtk_window_set_resizable(remminamain->window, FALSE);
	}
	/* New Button */
	remminamain->button_new = GTK_BUTTON(RM_GET_OBJECT("button_new"));
	if (kioskmode && kioskmode == TRUE)
		gtk_widget_set_sensitive(GTK_WIDGET(remminamain->button_new), FALSE);
	/* Search bar */
	remminamain->search_toggle = GTK_TOGGLE_BUTTON(RM_GET_OBJECT("search_toggle"));
	remminamain->search_bar = GTK_SEARCH_BAR(RM_GET_OBJECT("search_bar"));
	/* view mode list/tree */
	remminamain->view_toggle_button = GTK_TOGGLE_BUTTON(RM_GET_OBJECT("view_toggle_button"));
	if (kioskmode && kioskmode == TRUE)
		gtk_widget_set_sensitive(GTK_WIDGET(remminamain->view_toggle_button), FALSE);

	/* Menu widgets */
	remminamain->menu_popup = GTK_MENU(RM_GET_OBJECT("menu_popup"));
	remminamain->menu_header_button = GTK_MENU_BUTTON(RM_GET_OBJECT("menu_header_button"));
	remminamain->menu_popup_full = GTK_MENU(RM_GET_OBJECT("menu_popup_full"));
	remminamain->menu_popup_multi = GTK_MENU(RM_GET_OBJECT("menu_popup_multi"));
	if (kioskmode && kioskmode == TRUE) {
		gtk_widget_set_sensitive(GTK_WIDGET(remminamain->menu_popup_full), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(remminamain->menu_header_button), FALSE);
	}
	/* View mode radios */
	remminamain->menuitem_view_mode_list = GTK_RADIO_MENU_ITEM(RM_GET_OBJECT("menuitem_view_mode_list"));
	remminamain->menuitem_view_mode_tree = GTK_RADIO_MENU_ITEM(RM_GET_OBJECT("menuitem_view_mode_tree"));
	/* Quick connect objects */
	remminamain->box_quick_connect = GTK_BOX(RM_GET_OBJECT("box_quick_connect"));
	remminamain->combo_quick_connect_protocol = GTK_COMBO_BOX_TEXT(RM_GET_OBJECT("combo_quick_connect_protocol"));
	if (kioskmode && kioskmode == TRUE)
		gtk_widget_set_sensitive(GTK_WIDGET(remminamain->combo_quick_connect_protocol), FALSE);
	remminamain->entry_quick_connect_server = GTK_ENTRY(RM_GET_OBJECT("entry_quick_connect_server"));
	/* Other widgets */
	remminamain->tree_files_list = GTK_TREE_VIEW(RM_GET_OBJECT("tree_files_list"));
	remminamain->column_files_list_name = GTK_TREE_VIEW_COLUMN(RM_GET_OBJECT("column_files_list_name"));
	remminamain->column_files_list_group = GTK_TREE_VIEW_COLUMN(RM_GET_OBJECT("column_files_list_group"));
	remminamain->column_files_list_server = GTK_TREE_VIEW_COLUMN(RM_GET_OBJECT("column_files_list_server"));
	remminamain->column_files_list_plugin = GTK_TREE_VIEW_COLUMN(RM_GET_OBJECT("column_files_list_plugin"));
	remminamain->column_files_list_date = GTK_TREE_VIEW_COLUMN(RM_GET_OBJECT("column_files_list_date"));
	remminamain->column_files_list_notes = GTK_TREE_VIEW_COLUMN(RM_GET_OBJECT("column_files_list_notes"));
	gtk_tree_view_column_set_fixed_width(remminamain->column_files_list_notes, 100);
	remminamain->statusbar_main = GTK_STATUSBAR(RM_GET_OBJECT("statusbar_main"));
	/* signals */
	g_signal_connect(remminamain->entry_quick_connect_server, "key-release-event", G_CALLBACK(remmina_main_search_key_event), NULL);
	g_signal_connect(remminamain->tree_files_list, "row-activated", G_CALLBACK(remmina_main_tree_row_activated), NULL);
	/* Non widget objects */
	actions = g_simple_action_group_new();
	g_action_map_add_action_entries(G_ACTION_MAP(actions), app_actions, G_N_ELEMENTS(app_actions), remminamain->window);
	gtk_widget_insert_action_group(GTK_WIDGET(remminamain->window), "app", G_ACTION_GROUP(actions));
	g_action_map_add_action_entries(G_ACTION_MAP(actions), main_actions, G_N_ELEMENTS(main_actions), remminamain->window);
	gtk_widget_insert_action_group(GTK_WIDGET(remminamain->window), "main", G_ACTION_GROUP(actions));
	g_object_unref(actions);
	/* Accelerators */
	accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(remminamain->window, accel_group);
	gtk_accel_group_connect(accel_group, GDK_KEY_Q, GDK_CONTROL_MASK, 0,
				g_cclosure_new_swap(G_CALLBACK(remmina_main_on_action_application_quit), NULL, NULL));
	// TODO: This crash remmina because the function doesn't receive the parameter we expect
	gtk_accel_group_connect(accel_group, GDK_KEY_P, GDK_CONTROL_MASK, 0,
				g_cclosure_new_swap(G_CALLBACK(remmina_main_on_accel_application_preferences), NULL, NULL));
	gtk_accel_group_connect(accel_group, GDK_KEY_F, GDK_CONTROL_MASK, 0,
				g_cclosure_new_swap(G_CALLBACK(remmina_main_on_accel_search_toggle), remminamain, NULL));

	/* Connect signals */
	gtk_builder_connect_signals(remminamain->builder, NULL);
	/* Initialize the window and load the preferences */
	remmina_main_init();
	return GTK_WIDGET(remminamain->window);
}

GtkWindow *remmina_main_get_window()
{
	if (!remminamain)
		return NULL;
	if (!remminamain->priv)
		return NULL;
	if (!remminamain->priv->initialized)
		return NULL;
	remminamain->window = GTK_WINDOW(RM_GET_OBJECT("RemminaMain"));
	return remminamain->window;
}

void remmina_main_update_file_datetime(RemminaFile *file)
{
	if (!remminamain)
		return;
	remmina_main_load_files();
}

void remmina_main_show_dialog(GtkMessageType msg, GtkButtonsType buttons, const gchar* message) {
	GtkWidget *dialog;

	if (remminamain->window) {
		dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, msg, buttons, "%s", message);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
}

void remmina_main_show_warning_dialog(const gchar *message) {
    GtkWidget *dialog;

    if (remminamain->window) {
        dialog = gtk_message_dialog_new(remminamain->window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
                                        message, g_get_application_name());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}
