Tools for Soarer's Converter
============================

Synopsis
--------
```
$ sctool
Usage: sctool command [command options...]

  Options:
    -h                   Show this message.

  Commands:
     boot                Cause the device to reboot to bootloader
     info                Get device info
     listen              Listen for keypresses
     read <output file>  Read the current config from EEPROM
     write <input file>  Write the given file to EEPROM

$ scas <input file> <output file>

$ scdis <input file> <output file>
```

Description
-----------

``sctool`` is a replacement for the original [Soarer's converter tools](https://geekhack.org/index.php?topic=17458.msg335414#msg335414).
I've also included versions of the original ``scas`` and ``scdis`` with some minor
improvements.

I've included the [original documentation](./docs), but it can be dramatically
simplified. See below for instructions on how to use ``sctool`` to update your
converter's configuration.

The original example configurations can be found in the [configs](./configs)
directory.

Dependencies
------------

- autoconf / automake / libtool
- [hidapi](https://github.com/signal11/hidapi)

The bundled version of hidapi will be built if hidapi is not present.

Licensing
---------

See the [LICENSE](./LICENSE) file for details. The bundled hidapi is distributed
under its included BSD license.

Installation
------------

Building ``sctools`` is as simple as:
```
$ ./autogen.sh && make && make install
```

Note that you can pass arguments to ``configure`` via autogen.sh. for
example:
```
$ ./autogen.sh --prefix=/usr --sysconfdir=/etc
```

On Linux, udev rules for the converter will also be installed in
``$(sysconfdir)/udev/rules.d`` which will give cause the converter to be
owned by the ``plugdev`` group, with permissions of 0660. Thus, you may
want to pass ``--sysconfdir=/etc`` to configure.

Looking up Keys
---------------

The `` listen`` command will output data in the following format, with the
keysym used by ``scas`` between parenthesis.
```
rF0 r5A -28 (ENTER) u28
```

Updating the Configuration
--------------------------

To update the configuration, just do the following:
```
$ scas my_config.sc my_config.bin
scas v1.10
No errors. Wrote: my_config.bin

$ sctool write my_config.bin
Soarer's Converter Tool v1.0

---- Write (54 bytes) ----
Device ready
54 / 54 bytes written
Transfer complete
```

Now, the new configuration should be applied.

Known Issues
------------

- For some reason, the ``hidraw`` variant of hidapi doesn't enumerate the
device corresponding to the interface that the ``listen`` command uses.

- ``scdis`` may be buggy with handling some macro blocks.


