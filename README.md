# glibcrun

`glibcrun` is utility for launching glibc linked binaries in isolated namespaces
in musl-libc Void Linux installations.

It creates new private mount namespace for the running process, "replacing" `/usr`
and `/var/db/xbps` with directories from your glibc basedir using bind
mounts, and launches your glibc program.

## Creating glibc chroot

I will use `/glibc` directory name for an example, you can use any other path you
want.

```
# mkdir /glibc
# XBPS_ARCH=x86_64 xbps-install --repository=http://alpha.de.repo.voidlinux.org/current -r /glibc -S base-voidstrap
```

When it's done you may want to chroot into it, e.g. to install some dependencies
for your glibc software.

## Installing glibcrun

Just clone the repo, and then:

```
$ make
$ sudo make install
```

Note that installed binary must be owned as root and have suid bit. `make install`
should handle it, but  anyway.

## Usage

`glibcrun` needs to know the path to your glibc base directory and it reads it from
the `GLIBCRUN_DIR` environment variable. You may want to add something like this
to your `~/.bashrc` or similar script:

```
export GLIBCRUN_DIR=/glibc
```

When `glibcrun` is run without arguments it will attempt to launch a shell from your
`SHELL` variable, otherwise it will treat the first argument as a path to an executable
and the rest as a list of arguments.

Example:

```
glibcrun /opt/palemoon/palemoon -ProfileManager
```

will launch `/opt/palemoon/palemoon -ProfileManager`.

## License

BSD-2c
