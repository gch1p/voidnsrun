# voidnsrun

**voidnsrun** is utility for launching programs in an isolated mount namespace
with alternative `/usr` tree. Its primary goal is to run glibc programs in
musl-libc Void Linux environments (or vice-versa, but who needs that?).

It creates a new private mount namespace, transparently substitutes `/usr` and 
some other directories with directories from your glibc container using bind
mounts, and launches your program.

**voidnsundo**, to the contrary, is utility for launching programs in the parent
mount namespace from within the mount namespace created by **voidnsrun**.

## Installation

### Creating glibc container

As per the [Void documentation](https://docs.voidlinux.org/installation/musl.html#glibc-chroot),
perform glibc base system installation to a separate new directory:
```
# mkdir /glibc
# XBPS_ARCH=x86_64 xbps-install --repository=http://alpha.de.repo.voidlinux.org/current -r /glibc -S base-voidstrap
```

### Installing voidnsrun

Clone the repo, and then:

```
make run
sudo make install-run
```

This will install **voidnsrun** to `/usr/local/bin`.

Export path to the container:
```
export VOIDNSRUN_DIR=/glibc
```

Also export path to **voidnsundo**:
```
export VOIDNSUNDO_BIN=/usr/local/bin/voidnsundo
```

You may want to add these exports to your `~/.bashrc` or similar script.

### Installing voidnsundo

**voidnsundo** is supposed to be used from within the glibc container, so it has
to be linked with glibc. First, let's use just installed **voidnsrun** to install
build dependencies into the container:
```
sudo voidnsrun -r /glibc xbps-install -Su
sudo voidnsrun -r /glibc xbps-install make gcc
```

Then enter the container (the current working directory will be preserved by
**voidnsrun** 1.2 or higher) and build, then install **voidnsundo**:
```
voidnsrun bash
make clean
make undo
sudo make install-undo
```

This will install **voidnsundo** to `/usr/local/bin` in the container (which is
`/glibc/usr/local/bin` in reality).

Type `exit` or `Ctrl+D` to exit the container.

## Usage

### voidnsrun 
```
Usage: voidnsrun [OPTIONS] PROGRAM [ARGS]

Options:
    -r <path>: Container path. When this option is not present,
               VOIDNSRUN_DIR environment variable is used.
    -m <path>: Add bind mount. You can add up to 50 paths.
    -u <path>: Add undo bind mount. You can add up to 50 paths.
    -U <path>: Path to voidnsundo. When this option is not present,
               VOIDNSUNDO_BIN environment variable is used.
    -i:        Don't treat missing source or target for added mounts as error.
    -V:        Enable verbose output.
    -h:        Print this help.
    -v:        Print version.
```

**voidnsrun** needs to know the path to your glibc installation directory (or
"container"), it can read it from the `VOIDNSRUN_DIR` environment variable or
you can use `-r` argument to specify it.

By default, **voidnsrun** binds only `/usr` from the container. But if you're
launching `xbps-install`, `xbps-remove` or `xbps-reconfigure`and using
**voidnsrun** version 1.1 or higher, it will bind `/usr`, `/var` and `/etc`.

To bind something else, use the `-m` option. You can add up to 50 binds as of
version 1.2.

There's also the `-u` option. It adds bind mounts of the **voidnsundo** binary
inside the namespace. See more about this below in the **voidnsundo** bind mode
section. Just like with the `-m` option, you can add up to 50 binds as of version
1.2.

To bind the **voidnsundo** binary, **voidnsrun** has to know its path, and, like
with the container's path, it reads it from the `VOIDNSUNDO_BIN` environment
variable and from the `-U` option.

### voidnsundo

```
Usage: voidnsundo [OPTIONS] PROGRAM [ARGS]

Options:
    -V:  Enable verbose output.
    -h:  Print this help.
    -v:  Print version.
```

**voidnsundo** can be used in two modes.

One is the **"normal" node**, when you invoke it like `voidnsundo <PROGRAM> [ARGS]`
and your `PROGRAM` will be launched from and in the original mount namespace. 

For example, if you don't have a glibc version of firefox installed (so there's
no `/usr/bin/firefox` in the container), but you want to launch the "real" (the
one installed in your root musl system) firefox while being in the mount
namespace, just do `voidnsundo /usr/bin/firefox`.

The other mode is the **"bind" mode**. While in the container, and therefore in
the new mount namespace, you can bind mount **voidnsundo** to any path (don't
worry: it won't be visible outside the namespace), and, when invoked by that
path, it will launch the corresponding executable in your parent (root)
namespace.

For example, being in the container, you can do this:
```
touch /usr/bin/firefox
mount --bind /usr/local/bin/voidnsundo /usr/bin/firefox
```
and while there was no `/usr/bin/firefox` in the glibc container, after this,
when you'll launch `/usr/bin/firefox`, the "real" firefox from the root musl
system will be launched.

The creation of this bind mounts of **voidnsundo** can be automated by using
`-u` option of **voidnsrun**.

## Examples

This section contains some real examples of how to use some proprietary glibc
apps on your musl-libc Void Linux box.

### Vivaldi

The first example is the Vivaldi browser. Let's assume you unpacked it to
`/opt/vivaldi` (from rpm or deb package) and, obviously, it doesn't work.

Try launching it with **voidnsrun**:
```
$ voidnsrun /opt/vivaldi/vivaldi
```

It won't work just yet, but it's a start:
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

Sync repos and install `glib`:
```
$ sudo voidnsrun -r /glibc xbps-install -Su
$ sudo voidnsrun -r /glibc xbps-install glib
```

Try launching vivaldi again:
```
$ voidnsrun /opt/vivaldi/vivaldi
/opt/vivaldi/vivaldi: error while loading shared libraries: libnss3.so: cannot open shared object file: No such file or directory
```

As you can see, it no longer complains about missing `libgobject-2.0.so.0`, now 
it's `libnss3.so`. Repeat steps above for all missing dependencies, and in the
end, it will work.

Note that, for some reason, it doesn't complain about missing font related
libraries, such as freetype, so make sure to install them too, as well as some
base fonts:
```
$ sudo voidnsrun -r /glibc xbps-install freetype fontconfig libXft xorg-fonts
```

If you're noticing performance issues with Vivaldi, check the `vivaldi://gpu`
page. If it turns out that hardware acceleration is unavailable, you're missing
some packages again. I don't know which ones exactly, but installing `xorg-minimal`
should fix it.

### PhpStorm

**PhpStorm** and other JetBrains IDEs should just work like this (of course,
replace `/opt/PhpStorm` with real path on your machine):
```
voidnsrun /opt/PhpStorm/bin/phpstorm.sh
```

But it is only at first glance, everything works. After some time you may
notice all kinds of weird stuff caused by the fact that it runs inside the
"container" with different `/usr`. For instance, if you open built-in terminal
window, it will work, but... it will not be the shell you expect, it will be
glibc-linked shell from the container. Some programs that you have
installed on your root musl system will not be available there (like, it won't be
able to launch a browser because there's no browser), other may not work as
expected. 

In general, all programs that launch other programs will suffer from this. To
overcome this, the **voidnsundo** utility has been written and `-u` option added
to **voidnsrun**.

To fix the built-in PhpStorm's terminal and the ability to launch browser as shown
in the above example, launch it like so:
```
voidnsrun -u /bin/bash -u /usr/bin/firefox /opt/PhpStorm/bin/phpstorm.sh
```

## FAQ

#### Q: `sudo voidnsrun xbps-install` exits with "environment variable VOIDNSRUN_DIR not found" error

A: Add this line to `/etc/sudoers`:
```
Defaults env_keep += "VOIDNSRUN_DIR"
```

#### Q: Why applications launched with voidnsrun do not see my fonts?

A: If you installed fonts on your main system, applications that run in the mount
namespace can't see them because of custom `/usr` directory. You need to install
them again into the container directory.

Some workaround to bind-mount `/usr/share/fonts` from the root system to the
namespace may be introduced in future, if Linux will allow such hacks.

## Security

**voidnsrun** and **voidnsundo** are setuid applications, meaning they are
actually started as root and then dropping privileges when they can. setuid is
generally bad, it's a common attack vector that allows local privilege
escalation by exploiting unsafe code of setuid programs.

While these utilities have been written with this thought in mind, don't trust
me. Read the code, it's not too big and it's commented. Place yourself in
attacker's shoes and try to find a hole. For every new discovered vulnerability
in these utilities that would allow privilege escalation or something similar I
promise to pay $25 in Bitcoin. Contact me if you find something.

## Changelog

#### 1.2

- Added **voidnsundo** utility for spawning programs in the parent mount
  namespace from within the namespace created by **voidnsrun**.
- Restore current working directory after changing namespace.

#### 1.1

- Bind whole `/etc` and `/var` when launching `xbps-install`, `xbps-remove` or 
  `xbps-reconfigure`.

#### 1.0

- Initial release.

## License

BSD-2c
