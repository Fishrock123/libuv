/* Copyright the libuv project. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "task.h"

/* FIXME we shouldn't need to branch in this file */
#if defined(__unix__) || defined(__POSIX__) || \
    defined(__APPLE__) || defined(_AIX)
#include <unistd.h> /* unlink, rmdir, memset */
#else
# include <direct.h>
# include <io.h>
# define unlink _unlink
# define rmdir _rmdir
#endif

#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

static uv_dir_t dir;

static uv_dirent_t dirents[1];

static int empty_opendir_cb_count;
static int empty_readdir_cb_count;
static int empty_closedir_cb_count;

static void empty_closedir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ++empty_closedir_cb_count;
}

static void empty_readdir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->fs_type == UV_FS_READDIR);

  /* TODO jgilli: by default, uv_fs_readdir doesn't return 0 when reading
     an empty dir. Instead, it returns "." and ".." entries in sequence.
     Should this be changed to mimic uv_fs_scandir's behavior? */
  if (req->result != 0) {
    ASSERT(req->result == 1);
    ASSERT(req->dirents == dirents);

#ifdef HAVE_DIRENT_TYPES
    // In an empty directory, all entries are directories ("." and "..")
    ASSERT(dirents[0].type == UV_DIRENT_DIR);
#else
    ASSERT(dirents[0].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */

    ++empty_readdir_cb_count;

    uv_fs_req_cleanup((uv_fs_t*) req);

    uv_fs_readdir(uv_default_loop(),
                  req,
                  dirents,
                  ARRAY_SIZE(dirents),
                  empty_readdir_cb);
  } else {
    ASSERT(empty_readdir_cb_count == 2);
    uv_fs_closedir(uv_default_loop(), req,
                   empty_closedir_cb);

    uv_fs_dir_cleanup(req);
  }
}

static void empty_opendir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == 0);
  ASSERT(req->dir != NULL);

  uv_fs_req_cleanup((uv_fs_t*) req);
  ASSERT(0 == uv_fs_readdir(uv_default_loop(),
                            req,
                            dirents,
                            ARRAY_SIZE(dirents),
                            empty_readdir_cb));
  uv_fs_dir_cleanup(req);
  ++empty_opendir_cb_count;
}

/*
 * This test makes sure that both synchronous and asynchronous flavors
 * of the uv_fs_opendir -> uv_fs_readdir -> uv_fs_closedir sequence work
 * as expected when processing an empty directory.
 */
TEST_IMPL(fs_readdir_empty_dir) {
  const char* path;
  uv_fs_t mkdir_req;
  uv_fs_t rmdir_req;
  int r;
  int nb_entries_read;
  int entry_idx;
  size_t entries_count;

  path = "./empty_dir/";

  uv_fs_mkdir(uv_default_loop(), &mkdir_req, path, 0777, NULL);
  uv_fs_req_cleanup(&mkdir_req);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(uv_default_loop(),
                    &dir,
                    path,
                    NULL);
  ASSERT(r == 0);
  ASSERT(dir.fs_type == UV_FS_OPENDIR);
  ASSERT(dir.result == 0);
  ASSERT(dir.dir != NULL);
  uv_fs_req_cleanup((uv_fs_t*) &dir);

  entries_count = 0;
  nb_entries_read = uv_fs_readdir(uv_default_loop(),
                                  &dir,
                                  dirents,
                                  ARRAY_SIZE(dirents),
                                  NULL);
  uv_fs_req_cleanup((uv_fs_t*) &dir);

  while (0 != nb_entries_read) {
    entry_idx = 0;
    while (entry_idx < nb_entries_read) {
#ifdef HAVE_DIRENT_TYPES
      // In an empty directory, all entries are directories ("." and "..")
      ASSERT(dirents[entry_idx].type == UV_DIRENT_DIR);
#else
      ASSERT(dirents[entry_idx].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */
      ++entry_idx;
      ++entries_count;
    }

    nb_entries_read =  uv_fs_readdir(uv_default_loop(),
                                     &dir,
                                     dirents,
                                     ARRAY_SIZE(dirents),
                                     NULL);
  }
  uv_fs_req_cleanup((uv_fs_t*) &dir);

  /*
   * TODO jgilli: by default, uv_fs_readdir doesn't return UV_EOF when reading
   * an empty dir. Instead, it returns "." and ".." entries in sequence.
   * Should this be changed to mimic uv_fs_scandir's behavior?
   */
  ASSERT(entries_count == 2);

  uv_fs_closedir(uv_default_loop(), &dir, NULL);
  ASSERT(dir.result == 0);
  uv_fs_dir_cleanup(&dir);

  /* Testing the asynchronous flavor */

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  r = uv_fs_opendir(uv_default_loop(), &dir, path, empty_opendir_cb);
  ASSERT(r == 0);

  ASSERT(empty_opendir_cb_count == 0);
  ASSERT(empty_closedir_cb_count == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(empty_opendir_cb_count == 1);
  ASSERT(empty_closedir_cb_count == 1);

  uv_fs_rmdir(uv_default_loop(), &rmdir_req, path, NULL);
  uv_fs_req_cleanup(&rmdir_req);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

/*
 * This test makes sure that reading a non-existing directory with
 * uv_fs_{open,read}_dir returns proper error codes.
 */

static int non_existing_opendir_cb_count;

static void non_existing_opendir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == UV_ENOENT);
  ASSERT(req->dir == NULL);

  uv_fs_dir_cleanup(req);
  ++non_existing_opendir_cb_count;
}

TEST_IMPL(fs_readdir_non_existing_dir) {
  const char* path;
  int r;

  path = "./non-existing-dir/";

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(uv_default_loop(), &dir, path, NULL);

  ASSERT(r == UV_ENOENT);
  ASSERT(dir.fs_type == UV_FS_OPENDIR);
  ASSERT(dir.result == UV_ENOENT);
  ASSERT(dir.dir == NULL);
  uv_fs_req_cleanup((uv_fs_t*) &dir);

  /* Testing the async flavor */
  r = uv_fs_opendir(uv_default_loop(), &dir, path,
                    non_existing_opendir_cb);
  ASSERT(r == 0);
  ASSERT(non_existing_opendir_cb_count == 0);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(non_existing_opendir_cb_count == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

/*
 * This test makes sure that reading a file as a directory reports correct
 * error codes.
 */

static int file_opendir_cb_count;

static void file_opendir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == UV_ENOTDIR);
  ASSERT(req->dir == NULL);

  uv_fs_dir_cleanup(req);
  ++file_opendir_cb_count;
}

TEST_IMPL(fs_readdir_file) {
  const char* path;
  int r;

  path = "test/fixtures/empty_file";

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(uv_default_loop(), &dir, path, NULL);

  ASSERT(r == UV_ENOTDIR);
  ASSERT(dir.fs_type == UV_FS_OPENDIR);
  ASSERT(dir.result == UV_ENOTDIR);
  ASSERT(dir.dir == NULL);
  uv_fs_req_cleanup((uv_fs_t*) &dir);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  /* Testing the async flavor */
  r = uv_fs_opendir(uv_default_loop(), &dir, path, file_opendir_cb);
  ASSERT(r == 0);

  ASSERT(file_opendir_cb_count == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(file_opendir_cb_count == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

/*
 * This test makes sure that reading a non-empty directory with
 * uv_fs_{open,read}_dir returns proper directory entries, including the
 * correct entry types.
 */

static int non_empty_opendir_cb_count;
static int non_empty_readdir_cb_count;
static int non_empty_closedir_cb_count;

static void non_empty_closedir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->result == 0);

  ++non_empty_closedir_cb_count;
}

static void non_empty_readdir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->fs_type == UV_FS_READDIR);

  // TODO jgilli: by default, uv_fs_readdir doesn't return UV_EOF when reading
  // an empty dir. Instead, it returns "." and ".." entries in sequence.
  // Should this be fixed to mimic uv_fs_scandir's behavior?
  if (req->result == 0) {
    ASSERT(non_empty_readdir_cb_count == 5);
    uv_fs_req_cleanup((uv_fs_t*) req);
    uv_fs_closedir(uv_default_loop(), req,
                   non_empty_closedir_cb);
  } else {
    ASSERT(req->result == 1);
    ASSERT(req->dirents == dirents);

#ifdef HAVE_DIRENT_TYPES
    if (!strcmp(dirents[0].name, "test_subdir") ||
        !strcmp(dirents[0].name, ".") ||
        !strcmp(dirents[0].name, "..")) {
      ASSERT(dirents[0].type == UV_DIRENT_DIR);
    } else {
      ASSERT(dirents[0].type == UV_DIRENT_FILE);
    }
#else
    ASSERT(dirents[0].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */

    ++non_empty_readdir_cb_count;

    uv_fs_req_cleanup((uv_fs_t*) req);
    uv_fs_readdir(uv_default_loop(),
                  req,
                  dirents,
                  ARRAY_SIZE(dirents),
                  non_empty_readdir_cb);
  }
}

static void non_empty_opendir_cb(uv_dir_t* req) {
  ASSERT(req == &dir);
  ASSERT(req->fs_type == UV_FS_OPENDIR);
  ASSERT(req->result == 0);
  ASSERT(req->dir != NULL);
  uv_fs_req_cleanup((uv_fs_t*) req);
  ASSERT(0 == uv_fs_readdir(uv_default_loop(),
                            req,
                            dirents,
                            ARRAY_SIZE(dirents),
                            non_empty_readdir_cb));
  ++non_empty_opendir_cb_count;
}

TEST_IMPL(fs_readdir_non_empty_dir) {
  int r;
  size_t entries_count;

  uv_fs_t mkdir_req;
  uv_fs_t rmdir_req;
  uv_fs_t create_req;
  uv_fs_t close_req;

  /* Setup */
  unlink("test_dir/file1");
  unlink("test_dir/file2");
  rmdir("test_dir/test_subdir");
  rmdir("test_dir");

  r = uv_fs_mkdir(uv_default_loop(), &mkdir_req, "test_dir", 0755, NULL);
  ASSERT(r == 0);

  /* Create 2 files synchronously. */
  r = uv_fs_open(uv_default_loop(), &create_req, "test_dir/file1",
                 O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR, NULL);
  ASSERT(r >= 0);
  uv_fs_req_cleanup(&create_req);
  r = uv_fs_close(uv_default_loop(), &close_req, create_req.result, NULL);
  ASSERT(r == 0);
  uv_fs_req_cleanup(&close_req);

  r = uv_fs_open(uv_default_loop(), &create_req, "test_dir/file2",
                 O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR, NULL);
  ASSERT(r >= 0);
  uv_fs_req_cleanup(&create_req);
  r = uv_fs_close(uv_default_loop(), &close_req, create_req.result, NULL);
  ASSERT(r == 0);
  uv_fs_req_cleanup(&close_req);

  r = uv_fs_mkdir(uv_default_loop(), &mkdir_req, "test_dir/test_subdir", 0755,
                  NULL);
  ASSERT(r == 0);

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  /* Testing the synchronous flavor */
  r = uv_fs_opendir(uv_default_loop(), &dir, "test_dir", NULL);

  ASSERT(r == 0);
  ASSERT(dir.fs_type == UV_FS_OPENDIR);
  ASSERT(dir.result == 0);
  ASSERT(dir.dir != NULL);
  uv_fs_req_cleanup((uv_fs_t*) &dir);
  // TODO jgilli: by default, uv_fs_readdir doesn't return UV_EOF when reading
  // an empty dir. Instead, it returns "." and ".." entries in sequence.
  // Should this be changed to mimic uv_fs_scandir's behavior?
  entries_count = 0;

  while (uv_fs_readdir(uv_default_loop(),
                       &dir,
                       dirents,
                       ARRAY_SIZE(dirents),
                       NULL) != 0) {
#ifdef HAVE_DIRENT_TYPES
    if (!strcmp(dirents[0].name, "test_subdir") ||
        !strcmp(dirents[0].name, ".") ||
        !strcmp(dirents[0].name, "..")) {
      ASSERT(dirents[0].type == UV_DIRENT_DIR);
    } else {
      ASSERT(dirents[0].type == UV_DIRENT_FILE);
    }
#else
    ASSERT(dirents[0].type == UV_DIRENT_UNKNOWN);
#endif /* HAVE_DIRENT_TYPES */
    ++entries_count;
  }

  ASSERT(entries_count == 5);
  uv_fs_req_cleanup((uv_fs_t*) &dir);

  uv_fs_closedir(uv_default_loop(), &dir, NULL);
  ASSERT(dir.result == 0);
  uv_fs_dir_cleanup(&dir);

  /* Testing the asynchronous flavor */

  /* Fill the req to ensure that required fields are cleaned up */
  memset(&dir, 0xdb, sizeof(dir));

  r = uv_fs_opendir(uv_default_loop(), &dir, "test_dir",
                    non_empty_opendir_cb);
  ASSERT(r == 0);

  ASSERT(non_empty_opendir_cb_count == 0);
  ASSERT(non_empty_closedir_cb_count == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(non_empty_opendir_cb_count == 1);
  ASSERT(non_empty_closedir_cb_count == 1);

  uv_fs_rmdir(uv_default_loop(), &rmdir_req, "test_subdir", NULL);
  uv_fs_req_cleanup(&rmdir_req);

  /* Cleanup */
  unlink("test_dir/file1");
  unlink("test_dir/file2");
  rmdir("test_dir/test_subdir");
  rmdir("test_dir");

  MAKE_VALGRIND_HAPPY();
  return 0;
 }
