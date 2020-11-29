# voidnsrun

`voidnsrun` is utility for launching programs in an isolated namespace with
alternative `/usr` tree. Its primary goal is to run glibc programs in
musl-libc Void Linux environments or vice-versa.

It creates a new private mount namespace, transparently substitutes `/usr` and 
some other directories with directories from your alternative root using bind
mounts, and launches your program.

## Installation

Just clone the repo, and then:

```
$ make
$ sudo make install
```

Note that installed binary must be owned by root and have suid bit. `make install`
should handle it.

## Usage

```
voidnsrun [OPTIONS] [PROGRAM [ARGS]]

Options:
	-h:        print this help
	-m <path>: add bind mount
	-r <path>: altroot path. If this option is not present,
	           VOIDNSRUN_DIR environment variable is used.
	-v:        print version
```

`voidnsrun` needs to know the path to your alternative root directory and it can
read it from the `VOIDNSRUN_DIR` environment variable or you can use `-r`
argument to specify it.

You may want to add something like this to your `~/.bashrc` or similar script:

```
export VOIDNSRUN_DIR=/glibc
```

By default, `voidnsrun` binds these paths from alternative root to the new
namespace:
- `/usr`
- `/var/db/xbps`
- `/etc/xbps.d`

If you want to mount something else, use `-m` argument.

## Example

Let's imagine you want to use some proprietary glibc app on your
musl-libc Void Linux box. Let it be Vivaldi browser for the example. You
unpacked it to `/opt/vivaldi` and it doesn't work, obviously.

First, you need to perform an alternative glibc base system installation to a
separate new directory:
```
# mkdir /glibc
# XBPS_ARCH=x86_64 xbps-install --repository=http://alpha.de.repo.voidlinux.org/current -r /glibc -S base-voidstrap
```

Export path to this installation for `voidnsrun`:
```
export VOIDNSRUN_DIR=/glibc
```

Try launching your app:
```
voidnsrun /opt/vivaldi/vivaldi
```

It won't work just yet:
```
/opt/vivaldi/vivaldi: error while loading shared libraries: libgobject-2.0.so.0: cannot open shared object file: No such file or directory
```

Now you need to install all its dependencies into your glibc installation. Use
`xlocate` from `xtools` package to find a package responsible for a file (or
just guess it):
```
$ xlocate libgobject-2.0.so.0
Signal-Desktop-1.38.1_1	/usr/lib/signal-desktop/resources/app.asar.unpacked/node_modules/sharp/vendor/lib/libgobject-2.0.so -> /usr/lib/signal-desktop/resources/app.asar.unpacked/node_modules/sharp/vendor/lib/libgobject-2.0.so.0.5600.4
Signal-Desktop-1.38.1_1	/usr/lib/signal-desktop/resources/app.asar.unpacked/node_modules/sharp/vendor/lib/libgobject-2.0.so.0 -> /usr/lib/signal-desktop/resources/app.asar.unpacked/node_modules/sharp/vendor/lib/libgobject-2.0.so.0.5600.4
Signal-Desktop-1.38.1_1	/usr/lib/signal-desktop/resources/app.asar.unpacked/node_modules/sharp/vendor/lib/libgobject-2.0.so.0.5600.4
glib-2.66.2_1	/usr/lib/libgobject-2.0.so.0 -> /usr/lib/libgobject-2.0.so.0.6600.2
glib-2.66.2_1	/usr/lib/libgobject-2.0.so.0.6600.2
glib-devel-2.66.2_1	/usr/share/gdb/auto-load/usr/lib/libgobject-2.0.so.0.6600.2-gdb.py
libglib-devel-2.66.2_1	/usr/lib/libgobject-2.0.so -> /usr/lib/libgobject-2.0.so.0
```

Sync repos and install `glib`. You can use `voidnsrun` for this purpose too.
Also, I think you should bind `/etc` and `/var` while using `xbps-install` via
`voidnsrun`, to not mess with your main system files.
```
$ sudo voidnsrun -r /glibc -m /etc -m /var xbps-install -Su
$ sudo voidnsrun -r /glibc -m /etc -m /var xbps-install glib
```

Try launching vivaldi again:
```
$ voidnsrun /opt/vivaldi/vivaldi
/opt/vivaldi/vivaldi: error while loading shared libraries: libnss3.so: cannot open shared object file: No such file or directory
```

As you can see, it no longer complains about missing `libgobject-2.0.so.0`, now 
it's `libnss3.so`. Repeat steps above for all missing dependencies, and in the
end, it will work. (If it's not, then something's still missing. In particular,
make sure to install fonts related packages: `xorg-fonts`, `freetype`,
`fontconfig`, `libXft`.)

## License

BSD-2c
