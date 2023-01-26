/* gpathbuf.c: A mutable path builder
 *
 * SPDX-FileCopyrightText: 2023  Emmanuele Bassi
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "gpathbuf.h"

#include "garray.h"
#include "gfileutils.h"
#include "ghash.h"
#include "gmessages.h"
#include "gstrfuncs.h"

/**
 * SECTION:gpathbuf
 * @Title: GPathBuf
 * @Short_description: A mutable path builder
 *
 * `GPathBuf` is a helper type that allows you to easily build paths from
 * individual elements, using the platform specific conventions for path
 * separator.
 *
 * |[<!-- language="C" -->
 * GPathBuf path;
 *
 * g_path_buf_init (&path);
 *
 * g_path_buf_push (&path, "usr");
 * g_path_buf_push (&path, "bin");
 * g_path_buf_push (&path, "echo");
 *
 * char *echo = g_path_buf_to_path (&path);
 * g_assert_cmpstr (echo, ==, "/usr/bin/echo");
 *
 * g_path_buf_clear (&path);
 * ]|
 *
 * You can also load a full path and then operate on its components:
 *
 * |[<!-- language="C" -->
 * GPathBuf path;
 *
 * g_path_buf_init_from_path (&path, "/usr/bin/echo");
 *
 * g_path_buf_pop (&path);
 * g_path_buf_push (&path, "sh");
 *
 * char *sh = g_path_buf_to_path (&path);
 * g_assert_cmpstr (sh, ==, "/usr/bin/sh");
 * ]|
 *
 * `GPathBuf` is available since GLib 2.76.
 */

typedef struct {
  GPtrArray *path;

  char *extension;

  gpointer padding[6];
} RealPathBuf;

G_STATIC_ASSERT (sizeof (GPathBuf) == sizeof (RealPathBuf));

#define PATH_BUF(b) ((RealPathBuf *) (b))

/**
 * g_path_buf_init:
 * @buf: a path buffer
 *
 * Initializes a `GPathBuf` instance.
 *
 * Returns: (transfer none): the initialized path builder
 *
 * Since: 2.76
 */
GPathBuf *
g_path_buf_init (GPathBuf *buf)
{
  RealPathBuf *rbuf = PATH_BUF (buf);

  rbuf->path = NULL;
  rbuf->extension = NULL;

  return buf;
}

/**
 * g_path_buf_init_from_path:
 * @buf: a path buffer
 * @path: (type filename) (nullable): a file system path
 *
 * Initializes a `GPathBuf` instance with the given path.
 *
 * Returns: (transfer none): the initialized path builder
 *
 * Since: 2.76
 */
GPathBuf *
g_path_buf_init_from_path (GPathBuf   *buf,
                           const char *path)
{
  g_return_val_if_fail (buf != NULL, NULL);

  g_path_buf_init (buf);

  if (path == NULL)
    return buf;
  else
    return g_path_buf_push (buf, path);
}

/**
 * g_path_buf_clear:
 * @buf: a path buffer
 *
 * Clears the contents of the path buffer.
 *
 * Since: 2.76
 */
void
g_path_buf_clear (GPathBuf *buf)
{
  RealPathBuf *rbuf = PATH_BUF (buf);

  g_return_if_fail (buf != NULL);

  g_clear_pointer (&rbuf->path, g_ptr_array_unref);
  g_clear_pointer (&rbuf->extension, g_free);
}

/**
 * g_path_buf_clear_to_path:
 * @buf: a path buffer
 *
 * Clears the contents of the path buffer and returns the built path
 *
 * Returns: (transfer full) (nullable) (type filename): the built path
 *
 * Since: 2.76
 */
char *
g_path_buf_clear_to_path (GPathBuf *buf)
{
  char *res;

  g_return_val_if_fail (buf != NULL, NULL);

  res = g_path_buf_to_path (buf);
  g_path_buf_clear (buf);

  return g_steal_pointer (&res);
}

/**
 * g_path_buf_new:
 *
 * Allocates a new `GPathBuf`.
 *
 * Returns: (transfer full): the newly allocated path buffer
 *
 * Since: 2.76
 */
GPathBuf *
g_path_buf_new (void)
{
  return g_path_buf_init (g_new (GPathBuf, 1));
}

/**
 * g_path_buf_new_from_path:
 * @path: (type filename): the path used to initialize the buffer
 *
 * Allocates a new `GPathBuf` with the given @path.
 *
 * Returns: (transfer full): the newly allocated path buffer
 *
 * Since: 2.76
 */
GPathBuf *
g_path_buf_new_from_path (const char *path)
{
  return g_path_buf_init_from_path (g_new (GPathBuf, 1), path);
}

/**
 * g_path_buf_free:
 * @buf: (not nullable): a path buffer
 *
 * Frees a `GPathBuf` allocated by g_path_buf_new().
 *
 * Since: 2.76
 */
void
g_path_buf_free (GPathBuf *buf)
{
  g_return_if_fail (buf != NULL);

  g_path_buf_clear (buf);
  g_free (buf);
}

/**
 * g_path_buf_free_to_path:
 * @buf: (not nullable): a path buffer
 *
 * Frees a `GPathBuf` allocated by g_path_buf_new(), and
 * returns the path inside the buffer.
 *
 * Returns: (transfer full) (nullable) (type filename): the path
 *
 * Since: 2.76
 */
char *
g_path_buf_free_to_path (GPathBuf *buf)
{
  char *res;

  g_return_val_if_fail (buf != NULL, NULL);

  res = g_path_buf_clear_to_path (buf);
  g_path_buf_free (buf);

  return g_steal_pointer (&res);
}

/**
 * g_path_buf_copy:
 * @buf: (not nullable): a path buffer
 *
 * Copies the contents of a path buffer into a new `GPathBuf`.
 *
 * Returns: (transfer full): the newly allocated path buffer
 *
 * Since: 2.76
 */
GPathBuf *
g_path_buf_copy (GPathBuf *buf)
{
  RealPathBuf *rbuf = PATH_BUF (buf);
  RealPathBuf *rcopy;
  GPathBuf *copy;

  g_return_val_if_fail (buf != NULL, NULL);

  copy = g_path_buf_new ();
  rcopy = PATH_BUF (copy);

  if (rbuf->path != NULL)
    {
      rcopy->path = g_ptr_array_new_null_terminated (rbuf->path->len, g_free, TRUE);
      for (guint i = 0; i < rbuf->path->len; i++)
        {
          const char *p = g_ptr_array_index (rbuf->path, i);

          if (p != NULL)
            g_ptr_array_add (rcopy->path, g_strdup (p));
        }
    }

  rcopy->extension = g_strdup (rbuf->extension);

  return copy;
}

/**
 * g_path_buf_push:
 * @buf: a path buffer
 * @path: (type filename): a path
 *
 * Extends the given path buffer with @path.
 *
 * If @path is absolute, it replaces the current path.
 *
 * If @path contains `G_DIR_SEPARATOR_S`, the buffer is extended by
 * as many elements the path provides.
 *
 * |[<!-- language="C" -->
 * GPathBuf buf, cmp;
 *
 * g_path_buf_init_from_path (&buf, "/tmp");
 * g_path_buf_push (&buf, ".X11-unix/X0");
 *
 * g_path_buf_init_from_path (&cmp, "/tmp/.X11-unix/X0");
 * g_assert_true (g_path_buf_equal (&buf, &cmp));
 *
 * g_path_buf_push (&buf, "/etc/locale.conf");
 * g_path_buf_init_from_path (&cmp, "/etc/locale.conf");
 * g_assert_true (g_path_buf_equal (&buf, &cmp));
 *
 * g_path_buf_clear (&buf);
 * g_path_buf_clear (&cmp);
 * ]|
 *
 * Returns: (transfer none): the modified path buffer
 *
 * Since: 2.76
 */
GPathBuf *
g_path_buf_push (GPathBuf   *buf,
                 const char *path)
{
  RealPathBuf *rbuf = PATH_BUF (buf);

  g_return_val_if_fail (buf != NULL, NULL);
  g_return_val_if_fail (path != NULL, buf);

  if (g_path_is_absolute (path))
    {
      char **elements = g_strsplit (path, G_DIR_SEPARATOR_S, -1);

#ifdef G_OS_UNIX
      /* strsplit() will add an empty element for the leading root,
       * which will cause the path build to ignore it; to avoid it,
       * we re-inject the root as the first element.
       *
       * The first string is empty, but it's still allocated, so we
       * need to free it to avoid leaking it.
       */
      g_free (elements[0]);
      elements[0] = g_strdup ("/");
#endif

      g_clear_pointer (&rbuf->path, g_ptr_array_unref);
      rbuf->path = g_ptr_array_new_null_terminated (g_strv_length (elements), g_free, TRUE);

      /* Skip empty elements caused by repeated separators */
      for (guint i = 0; elements[i] != NULL; i++)
        {
          if (*elements[i] != '\0')
            g_ptr_array_add (rbuf->path, g_steal_pointer (&elements[i]));
          else
            g_free (elements[i]);
        }

      g_free (elements);
    }
  else
    {
      char **elements = g_strsplit (path, G_DIR_SEPARATOR_S, -1);

      if (rbuf->path == NULL)
        rbuf->path = g_ptr_array_new_null_terminated (g_strv_length (elements), g_free, TRUE);

      /* An empty path element is assumed to be the current directory */
      if (elements == NULL || elements[0] == NULL)
        g_ptr_array_add (rbuf->path, g_strdup ("."));
      else
        {
          /* Skip empty elements caused by repeated separators */
          for (guint i = 0; elements[i] != NULL; i++)
            {
              if (*elements[i] != '\0')
                g_ptr_array_add (rbuf->path, g_steal_pointer (&elements[i]));
              else
                g_free (elements[i]);
            }
        }

      g_free (elements);
    }

  return buf;
}

/**
 * g_path_buf_pop:
 * @buf: a path buffer
 *
 * Removes the last element of the path buffer.
 *
 * If there is only one element in the path buffer (for example, `/` on Unix
 * systems), it will not be removed and %FALSE will be returned instead.
 *
 * |[<!-- language="C" -->
 * GPathBuf buf, cmp;
 *
 * g_path_buf_init_from_path (&buf, "/bin/sh");
 *
 * g_path_buf_pop (&buf);
 * g_path_buf_init_from_path (&cmp, "/bin");
 * g_assert_true (g_path_buf_equal (&buf, &cmp));
 *
 * g_path_buf_pop (&buf);
 * g_path_buf_init_from_path (&cmp, "/");
 * g_assert_true (g_path_buf_equal (&buf, &cmp));
 *
 * g_path_buf_clear (&buf);
 * g_path_buf_clear (&cmp);
 * ]|
 *
 * Returns: `TRUE` if the buffer was modified and `FALSE` otherwise
 *
 * Since: 2.76
 */
gboolean
g_path_buf_pop (GPathBuf *buf)
{
  RealPathBuf *rbuf = PATH_BUF (buf);

  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (rbuf->path != NULL, FALSE);

  /* Keep the first element of the buffer; it's either '/' or the drive */
  if (rbuf->path->len > 1)
    {
      g_ptr_array_remove_index (rbuf->path, rbuf->path->len - 1);
      return TRUE;
    }

  return FALSE;
}

/**
 * g_path_buf_set_filename:
 * @buf: a path buffer
 * @file_name: (type filename): the file name in the path
 *
 * Sets the file name of the path.
 *
 * Appends the given file name to the path in the buffer.
 *
 * If the previous file name was unset, this is the equivalent
 * of pushing @file_name.
 *
 * If the previous file name was set, this is the equivalent of
 * popping the path buffer and pushing @file_name, creating a
 * sibling of the original path.
 *
 * |[<!-- language="C" -->
 * GPathBuf buf, cmp;
 *
 * g_path_buf_init_from_path (&buf, "/");
 * g_path_buf_set_filename (&buf, "bar");
 *
 * g_path_buf_init_from_path (&cmp, "/bar");
 * g_assert_true (g_path_buf_equal (&buf, &cmp));
 *
 * g_path_buf_set_filename (&buf, "baz.txt");
 * g_path_buf_init_from_path (&cmp, "/baz.txt");
 * g_assert_true (g_path_buf_equal (&buf, &cmp);
 *
 * g_path_buf_clear (&buf);
 * g_path_buf_clear (&cmp);
 * ]|
 *
 * Returns: `TRUE` if the file name was replaced, and `FALSE` otherwise
 *
 * Since: 2.76
 */
gboolean
g_path_buf_set_filename (GPathBuf   *buf,
                         const char *file_name)
{
  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (file_name != NULL, FALSE);

  if (PATH_BUF (buf)->path == NULL)
    return FALSE;

  g_path_buf_pop (buf);
  g_path_buf_push (buf, file_name);

  return TRUE;
}

/**
 * g_path_buf_set_extension:
 * @buf: a path buffer
 * @extension: (type filename) (nullable): the file extension
 *
 * Adds an extension to the file name in the path buffer.
 *
 * If @extension is `NULL`, the extension will be unset.
 *
 * If the path buffer does not have a file name set, this function returns
 * `FALSE` and leaves the path buffer unmodified.
 *
 * Returns: `TRUE` if the extension was replaced, and `FALSE` otherwise
 *
 * Since: 2.76
 */
gboolean
g_path_buf_set_extension  (GPathBuf   *buf,
                           const char *extension)
{
  RealPathBuf *rbuf = PATH_BUF (buf);

  g_return_val_if_fail (buf != NULL, FALSE);

  if (rbuf->path != NULL)
    return g_set_str (&rbuf->extension, extension);
  else
    return FALSE;
}

/**
 * g_path_buf_to_path:
 * @buf: a path buffer
 *
 * Retrieves the path from the path buffer.
 *
 * Returns: (transfer full) (type filename) (nullable): the path
 *
 * Since: 2.76
 */
char *
g_path_buf_to_path (GPathBuf *buf)
{
  RealPathBuf *rbuf = PATH_BUF (buf);
  char *path = NULL;

  g_return_val_if_fail (buf != NULL, NULL);

  if (rbuf->path != NULL)
    path = g_build_filenamev ((char **) rbuf->path->pdata);

  if (path != NULL && rbuf->extension != NULL)
    {
      char *tmp = g_strconcat (path, ".", rbuf->extension, NULL);

      g_free (path);
      path = g_steal_pointer (&tmp);
    }

  return path;
}

/**
 * g_path_buf_equal:
 * @v1: (not nullable): a path buffer to compare
 * @v2: (not nullable): a path buffer to compare
 *
 * Compares two path buffers for equality and returns `TRUE`
 * if they are equal.
 *
 * This function can be passed to g_hash_table_new() as the
 * `key_equal_func` parameter.
 *
 * Returns: `TRUE` if the two path buffers are equal,
 *   and `FALSE` otherwise
 *
 * Since: 2.76
 */
gboolean
g_path_buf_equal (gconstpointer v1,
                  gconstpointer v2)
{
  if (v1 == v2)
    return TRUE;

  /* We resolve the buffer into a path to normalize its contents */
  char *p1 = g_path_buf_to_path ((GPathBuf *) v1);
  char *p2 = g_path_buf_to_path ((GPathBuf *) v2);

  gboolean res = p1 != NULL && p2 != NULL
               ? g_str_equal (p1, p2)
               : FALSE;

  g_free (p1);
  g_free (p2);

  return res;
}

/**
 * g_path_buf_hash:
 * @v: (not nullable): a path buffer
 *
 * Converts a path buffer to a hash value.
 *
 * It can be passed to g_hash_table_new() as the `hash_func` parameter,
 * when using non-`NULL` paths as keys in a #GHashTable.
 *
 * Returns: a hash value corresponding to the key
 *
 * Since: 2.76
 */
guint
g_path_buf_hash (gconstpointer v)
{
  GPathBuf *b = (GPathBuf *) v;
  char *p = g_path_buf_to_path (b);
  guint res = 0;

  if (p != NULL)
    res = g_str_hash (p);
  else
    res = GPOINTER_TO_UINT (b);

  g_free (p);

  return res;
}
