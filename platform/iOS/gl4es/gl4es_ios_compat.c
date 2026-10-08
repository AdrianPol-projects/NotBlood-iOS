// Bodies for the gl4es functions that are aliases on other platforms (see gl4es_ios_prefix.h).
#include "gl/directstate.h"

void gl4es_glEnableClientStatei(GLenum array, GLuint index)
{
    gl4es_glEnableClientStateIndexed(array, index);
}

void gl4es_glDisableClientStatei(GLenum array, GLuint index)
{
    gl4es_glDisableClientStateIndexed(array, index);
}
