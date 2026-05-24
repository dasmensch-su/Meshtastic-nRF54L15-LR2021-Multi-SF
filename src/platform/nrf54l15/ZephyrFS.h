#pragma once
/*
 * ZephyrFS — Arduino-compatible File/FS wrapper around Zephyr's LittleFS.
 *
 * Provides just enough of the Arduino File + FS API for Meshtastic's
 * FSCommon / SafeFile / NodeDB persistence layer to work unchanged.
 */

#include "Print.h"
#include <zephyr/fs/fs.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define FILE_O_READ  "r"
#define FILE_O_WRITE "w"

class File : public Print
{
  public:
    File() : _open(false), _size(0) {}

    bool open(const char *path, const char *mode)
    {
        strncpy(_path, path, sizeof(_path) - 1);
        _path[sizeof(_path) - 1] = '\0';
        _hash = 0;

        fs_mode_t flags;
        if (mode[0] == 'w') {
            flags = FS_O_CREATE | FS_O_WRITE;
            fs_unlink(path);
        } else {
            flags = FS_O_READ;
        }

        int rc = fs_open(&_fp, path, flags);
        if (rc < 0) {
            _open = false;
            return false;
        }
        _open = true;

        struct fs_dirent entry;
        if (fs_stat(path, &entry) == 0) {
            _size = entry.size;
        } else {
            _size = 0;
        }
        _read_pos = 0;
        return true;
    }

    void close()
    {
        if (_open) {
            fs_close(&_fp);
            _open = false;
        }
    }

    operator bool() const { return _open; }

    /* ---- Write (Print interface) ---- */

    size_t write(uint8_t c) override
    {
        if (!_open) return 0;
        ssize_t rc = fs_write(&_fp, &c, 1);
        return (rc == 1) ? 1 : 0;
    }

    size_t write(const uint8_t *buf, size_t len) override
    {
        if (!_open) return 0;
        ssize_t rc = fs_write(&_fp, buf, len);
        return (rc > 0) ? (size_t)rc : 0;
    }

    /* ---- Read ---- */

    int read()
    {
        if (!_open) return -1;
        uint8_t c;
        ssize_t rc = fs_read(&_fp, &c, 1);
        if (rc == 1) {
            _read_pos++;
            return c;
        }
        return -1;
    }

    int read(uint8_t *buf, size_t len)
    {
        if (!_open) return 0;
        ssize_t rc = fs_read(&_fp, buf, len);
        if (rc > 0) _read_pos += rc;
        return (rc >= 0) ? (int)rc : 0;
    }

    int available()
    {
        if (!_open) return 0;
        off_t cur = fs_tell(&_fp);
        if (cur < 0) return 0;
        struct fs_dirent entry;
        if (fs_stat(_path, &entry) == 0)
            return (int)(entry.size - cur);
        return 0;
    }

    size_t size() const { return _size; }

    void flush() override
    {
        if (_open) fs_sync(&_fp);
    }

    const char *name() const { return _path; }

    bool seek(size_t pos)
    {
        if (!_open) return false;
        return fs_seek(&_fp, (off_t)pos, FS_SEEK_SET) == 0;
    }

    size_t position()
    {
        if (!_open) return 0;
        off_t p = fs_tell(&_fp);
        return (p >= 0) ? (size_t)p : 0;
    }

    bool isDirectory() { return false; }

    File openNextFile() { return File(); }

  private:
    struct fs_file_t _fp = {};
    char _path[64];
    bool _open;
    size_t _size;
    size_t _read_pos;
    uint8_t _hash;
};

class ZephyrFS
{
  public:
    bool begin();

    File open(const char *path, const char *mode)
    {
        char fullpath[80];
        _makeFullPath(path, fullpath, sizeof(fullpath));

        File f;
        f.open(fullpath, mode);
        return f;
    }

    bool remove(const char *path)
    {
        char fullpath[80];
        _makeFullPath(path, fullpath, sizeof(fullpath));
        return fs_unlink(fullpath) == 0;
    }

    bool rename(const char *from, const char *to)
    {
        char fullFrom[80], fullTo[80];
        _makeFullPath(from, fullFrom, sizeof(fullFrom));
        _makeFullPath(to, fullTo, sizeof(fullTo));
        return fs_rename(fullFrom, fullTo) == 0;
    }

    bool mkdir(const char *path)
    {
        char fullpath[80];
        _makeFullPath(path, fullpath, sizeof(fullpath));
        return fs_mkdir(fullpath) == 0 || fs_mkdir(fullpath) == -EEXIST;
    }

    bool exists(const char *path)
    {
        char fullpath[80];
        _makeFullPath(path, fullpath, sizeof(fullpath));
        struct fs_dirent entry;
        return fs_stat(fullpath, &entry) == 0;
    }

    bool rmdir(const char *path)
    {
        char fullpath[80];
        _makeFullPath(path, fullpath, sizeof(fullpath));
        return fs_unlink(fullpath) == 0;
    }

    void rmdir_r(const char *path)
    {
        rmdir(path);
    }

  private:
    void _makeFullPath(const char *path, char *out, size_t outLen)
    {
        if (path[0] == '/' && strncmp(path, "/littlefs", 9) != 0) {
            snprintf(out, outLen, "/littlefs%s", path);
        } else {
            strncpy(out, path, outLen - 1);
            out[outLen - 1] = '\0';
        }
    }
};

extern ZephyrFS zephyrFS;
