// Force-included into every gl4es source file on iOS.
//
// Darwin's toolchain has no symbol aliases (__attribute__((alias))), which gl4es uses for
// exactly two functions; declare them plainly here and define them in gl4es_ios_compat.c.
#define AliasDecl(RET, NAME, DEF, OLD) RET NAME DEF
