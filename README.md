# Miga Shell

[![CI](https://github.com/spk121/migash/actions/workflows/ci.yml/badge.svg)](https://github.com/spk121/migash/actions/workflows/ci.yml)

The Miga Shell (commonly styled migash) aspires to be a
POSIX‑compliant shell, albeit with several distinguishing
characteristics of its own.

- It offers three configurations, each selectable at build time:
  - **POSIX mode**: a largely feature‑complete shell adhering to the
    full base POSIX specification.
  - **UCRT mode**: a Windows‑oriented shell providing only those
    features that may be implemented with relative ease upon UCRT and
    Win32; job control and redirection are, accordingly, rather
    limited.
  - **ISO C mode**: a most modest shell supporting only a subset of
    the POSIX specification, yet portable to any platform furnished
    with an ISO C compiler.
- It may also be compiled as a shared library, suitable for embedding
  within other applications, and presents a C API for such integration
  and control.

This project is presently **pre‑alpha** and not yet fit for general
use. The API remains unsettled, and the implementation is incomplete
and, without doubt, harbours a number of defects.

## Naming

The naming of things is ever a vexing affair, for nearly every
sensible name has already been claimed—particularly if one desires a
short appellation unencumbered by prior associations.

I selected the name *Miga Shell* because *miga* is a slang diminutive
of *amiga*, meaning “friend,” which was very much the sentiment
intended.

Yet, as the interwebs inform us, *miga* may also signify “crumb.” And
a search for *migash* yields references to Joseph ibn Migash, a
medieval Spanish rabbi.

## `migash` the shell

When built in POSIX mode, it is simply a shell: it presents a
command‑line interface, executes commands, and runs scripts written in
the shell language. In this regard, nothing is particularly
extraordinary.

In UCRT mode, it is more of a curiosity. That is because most of what
one thinks of a Windows command-line commands, like `dir`, are actually
builtins to the `cmd` shell or to PowerShell. So Miga Shell does function,
but there are few native Windows command-line commands to use with it.

The ISO C mode, though undeniably underpowered, holds interest for me
owing to my background in embedded systems and microcontrollers. I
wish to provide a shell interface suitable for such constrained
devices, and thus it must presume very little of the underlying
environment.

## `libmigash` Shell Library

This library may be linked into other applications. It offers a C API
through which one may supply builtin commands, provide input, and
receive the resulting output. If you can furnish a `FILE *` interface,
you may employ it interactively—either by crafting your own REPL loop
or by relying upon the one included within `libmigash`.

