FT5x06 Register Tool
===========

User-space tool used to write registers of FT5xxx touch controllers.

This code was largely inspired from the [Focaltech GitHub driver](https://github.com/focaltech-systems/drivers-input-touchscreen-FTS_driver).

Requirements
------------

The source code itself isn't depending any external library, so no build dependency.

As for runtime dependency, the kernel must have `i2c-dev` support enabled.

Build instructions
------------------

Very straight-forward:
```
$ cd
$ git clone https://github.com/xCuri0/ft5x06-register-tool
$ cd ft5x06-register-tool/
$ make
```

If you want to cross-compile the tool, simply provide a different `CC`.
```
$ CC=arm-linux-gnueabihf-gcc make
```

Finally, if you want to build it statically so it can run on any C library (like Android), just add the `LDFLAGS`.
```
$ LDFLAGS=-static CC=arm-linux-gnueabihf-gcc make
```

Usage
-----

The tool has an help output which lists all the different parameters.
```
FT5x06 tool usage: ft5x06-register-tool [OPTIONS]
OPTIONS:
	-a, --address
		I2C address of the FT5x06 controller (hex). Default is 0x38.
	-b, --bus
		I2C bus the FT5x06 controller is on. Default is 3.
	-r, --read
		Read register from address
	-w, --write
		Write to register at address
	-v, --value
		Value to write to register
	-h, --help
		Show this help and exit.
```

Here is an example on how to write to 0x88 to set the touch polling rate to 60hz:
```
# ft5x06-register-tool -w 0x88 -v 0x6
```

Another for reading that same value
```
# ft5x06-register-tool -r 0x88
```

Limitations
-----------

* This tool only was tested on a FT5435 found in a Redmi Note 4 Snapdragon running Lineage OS 17.1
