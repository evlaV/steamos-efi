# SteamOS Chainloader Compatibility

## Boot Locations

SteamOS has 1 top-level EFI partition and two
pseudo-EFI partitions (one pseudo-EFI per OS image).

The EFI partition should be the first EFI-compatible boot partition 
with a type of "EFI System" and has the stage 1 loader ("chainloader")
at the following locations:


### Fallback Boot Location

   EFI/boot/bootx64.efi

The first is the fallback location defined by UEFI, and is started
by UEFI firmware if no other (valid) configuration can be found.

### Vendor Boot Location

The second is the SteamOS vendor location for its loader. This
is, by convention:

   EFI/<name-of-os>/<name-of-loader>.efi

So:

   EFI/steamos/steamcl.efi

Normally nothing needs to be done to support this location since the fallback 
location has been set up … 

_but_ 

… if the system is set up to dual-boot and another OS takes over the fallback
path then it is useful for the UEFI firmware to have the SteamOS vendor path
hard-wired as well.

## Non-boot Locations

The two pseudo-EFI partitions have the second stage bootloader on them.
They are found at:

    EFI/steamos/grubx64.efi

It is not necessary for the firmware to expose these locations to the
user, or to select them directly. The chainloader takes care of that.

## Boot Menu

The chainloader has a boot menu which allows you to choose which of
the available OS images to start. It indicates which is the current 
image and when each image was last booted _as the default_.

It also allows you to request a manual boot from the stage 2 loader,
which allows you to do things like hand-edit the kernel command line
before booting the OS.

It can be triggered as follows:


### UEFI Command Line

Passing a command line parameter of "display-menu", UCS-2 encoded
to the chainloader.
    
In other words the following bytes:

    0000  64 00 69 00 73 00 70 00  6c 00 61 00 79 00 2d 00  |d.i.s.p.l.a.y.-.|
    0010  6d 00 65 00 6e 00 75 00                           |m.e.n.u.|

### Setting an NVRAM variable

A non-zero value for a well-known vendor NVRAM variable:

    UEFI Namespace: f0393d2c-78a4-4bb9-af08-2932ca0dc11e
    Variable Name:  JupiterFunctionConfigVariable
    Contents:       0xde 0xc1 0x01
    Variable type:  volatile [not required, but good practice]

You may also set this generically named value for the same effect
(although note that if you set both, JupiterFunctionConfigVariable
takes priority):

    UEFI Namespace: 399abb9b-4bee-4a18-ab5b-45c6e0e8c716
    Variable Name:  BootControl
    Contents:       0xde 0xc1 0x01
    Variable type:  volatile [not required, but good practice]

Note that the first two bytes are UEFI variable header data:
You _may_ not need to explicitly set them if your UEFI support 
takes care of that when assigning uint8 or byte values to UEFI 
NVRAM variables.

Since UEFI has no standard way to detect key up/key down events
or key-held-down state, this is the preferred way for the UEFI
firmware to signal to the chainloader that the user has requested
the boot menu.

### Repeated boot failures

If the current default OS image fails to boot 3 or more times in a row
the chainloader will display its menu (and explain why).

