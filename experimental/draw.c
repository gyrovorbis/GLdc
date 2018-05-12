#include <stdio.h>

#include "../include/gl.h"
#include "../include/glext.h"
#include "private.h"
#include "../gl-api.h"

typedef struct {
    const void* ptr;
    GLenum type;
    GLsizei stride;
    GLint size;
} AttribPointer;


static AttribPointer VERTEX_POINTER;
static AttribPointer UV_POINTER;
static AttribPointer ST_POINTER;
static AttribPointer NORMAL_POINTER;
static AttribPointer DIFFUSE_POINTER;

#define VERTEX_ENABLED_FLAG     (1 << 0)
#define UV_ENABLED_FLAG         (1 << 1)
#define ST_ENABLED_FLAG         (1 << 2)
#define DIFFUSE_ENABLED_FLAG    (1 << 3)
#define NORMAL_ENABLED_FLAG     (1 << 4)

static GLuint ENABLED_VERTEX_ATTRIBUTES = 0;
static GLubyte ACTIVE_CLIENT_TEXTURE = 0;

void initAttributePointers() {
    TRACE();

    VERTEX_POINTER.ptr = NULL;
    VERTEX_POINTER.stride = 0;
    VERTEX_POINTER.type = GL_FLOAT;
    VERTEX_POINTER.size = 4;
}

static GLuint byte_size(GLenum type) {
    switch(type) {
    case GL_BYTE: return sizeof(GLbyte);
    case GL_UNSIGNED_BYTE: return sizeof(GLubyte);
    case GL_SHORT: return sizeof(GLshort);
    case GL_UNSIGNED_SHORT: return sizeof(GLushort);
    case GL_INT: return sizeof(GLint);
    case GL_UNSIGNED_INT: return sizeof(GLuint);
    case GL_DOUBLE: return sizeof(GLdouble);
    default: return sizeof(GLfloat);
    }
}

static void transformVertex(GLfloat* src, float* x, float* y, float* z) {
    register float __x  __asm__("fr12");
    register float __y  __asm__("fr13");
    register float __z  __asm__("fr14");

    __x = src[0];
    __y = src[1];
    __z = src[2];

    mat_trans_fv12()

    *x = __x;
    *y = __y;
    *z = __z;
}

static void _parseColour(uint32* out, const GLubyte* in, GLint size, GLenum type) {
    switch(type) {
    case GL_BYTE: {
    case GL_UNSIGNED_BYTE:
        *out = in[3] << 24 | in[0] << 16 | in[1] << 8 | in[0];
    } break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        /* FIXME!!!! */
    break;
    case GL_INT:
    case GL_UNSIGNED_INT:
        /* FIXME!!!! */
    break;
    case GL_FLOAT:
    case GL_DOUBLE:
    default: {
        const GLfloat* src = (GLfloat*) in;
        *out = PVR_PACK_COLOR(src[3], src[0], src[1], src[2]);
    } break;
    }
}

static void _parseFloats(GLfloat* out, const GLubyte* in, GLint size, GLenum type) {
    switch(type) {
    case GL_SHORT: {
        GLshort* inp = (GLshort*) in;
        for(GLubyte i = 0; i < size; ++i) {
            out[i] = (GLfloat) inp[i];
        }
    } break;
    case GL_INT: {
        GLint* inp = (GLint*) in;
        for(GLubyte i = 0; i < size; ++i) {
            out[i] = (GLfloat) inp[i];
        }
    } break;
    case GL_FLOAT:
    case GL_DOUBLE:  /* Double == Float */
    default:
        for(GLubyte i = 0; i < size; ++i) out[i] = ((GLfloat*) in)[i];
    }
}

static void _parseIndex(GLshort* out, const GLubyte* in, GLenum type) {
    switch(type) {
    case GL_UNSIGNED_BYTE:
        *out = (GLshort) *in;
    break;
    case GL_UNSIGNED_SHORT:
    default:
        *out = *((GLshort*) in);
    }
}

static void submitVertices(GLenum mode, GLsizei first, GLsizei count, GLenum type, const GLvoid* indices) {
    static float normal[3];

    if(!(ENABLED_VERTEX_ATTRIBUTES & VERTEX_ENABLED_FLAG)) {
        return;
    }

    _glKosMatrixApplyRender(); /* Apply the Render Matrix Stack */   

    const GLsizei elements = (mode == GL_QUADS) ? 4 : (mode == GL_TRIANGLES) ? 3 : (mode == GL_LINES) ? 2 : count;

    // Make room for the element + the header
    PVRCommand* dst = (PVRCommand*) aligned_vector_extend(&activePolyList()->vector, count + 1);

    // Store a pointer to the header
    pvr_poly_hdr_t* hdr = (pvr_poly_hdr_t*) dst;

    // Point dest at the first new vertex to populate
    dst++;

    // Compile
    pvr_poly_cxt_t cxt = *getPVRContext();
    cxt.list_type = activePolyList()->list_type;
    updatePVRTextureContext(&cxt, getTexture0());

    pvr_poly_compile(hdr, &cxt);

    GLubyte vstride = (VERTEX_POINTER.stride) ? VERTEX_POINTER.stride : VERTEX_POINTER.size * byte_size(VERTEX_POINTER.type);
    const GLubyte* vptr = VERTEX_POINTER.ptr;

    GLubyte cstride = (DIFFUSE_POINTER.stride) ? DIFFUSE_POINTER.stride : DIFFUSE_POINTER.size * byte_size(DIFFUSE_POINTER.type);
    const GLubyte* cptr = DIFFUSE_POINTER.ptr;

    GLubyte uvstride = (UV_POINTER.stride) ? UV_POINTER.stride : UV_POINTER.size * byte_size(UV_POINTER.type);
    const GLubyte* uvptr = UV_POINTER.ptr;

    GLubyte nstride = (NORMAL_POINTER.stride) ? NORMAL_POINTER.stride : NORMAL_POINTER.size * byte_size(NORMAL_POINTER.type);
    const GLubyte* nptr = NORMAL_POINTER.ptr;

    const GLubyte* indices_as_bytes = (GLubyte*) indices;

    GLboolean lighting_enabled = isLightingEnabled();

    for(GLuint i = first; i < count; ++i) {
        pvr_vertex_t* vertex = (pvr_vertex_t*) dst;
        vertex->u = vertex->v = 0.0f;
        vertex->argb = PVR_PACK_COLOR(0.0f, 0.0f, 0.0f, 0.0f);
        vertex->oargb = 0;
        vertex->flags = PVR_CMD_VERTEX;

        if(((i + 1) % elements) == 0) {
            vertex->flags = PVR_CMD_VERTEX_EOL;
        }

        GLshort idx = i;
        if(indices) {
            _parseIndex(&idx, &indices_as_bytes[byte_size(type) * i], type);
        }

        _parseFloats(&vertex->x, vptr + (idx * vstride), VERTEX_POINTER.size, VERTEX_POINTER.type);
        transformVertex(&vertex->x, &vertex->x, &vertex->y, &vertex->z);

        if(ENABLED_VERTEX_ATTRIBUTES & DIFFUSE_ENABLED_FLAG) {
            _parseColour(&vertex->argb, cptr + (idx * cstride), DIFFUSE_POINTER.size, DIFFUSE_POINTER.type);
        }

        if(ENABLED_VERTEX_ATTRIBUTES & UV_ENABLED_FLAG) {
            _parseFloats(&vertex->u, uvptr + (idx * uvstride), UV_POINTER.size, UV_POINTER.type);
        }

        if(ENABLED_VERTEX_ATTRIBUTES & NORMAL_ENABLED_FLAG) {
            _parseFloats(normal, nptr + (idx * nstride), NORMAL_POINTER.size, NORMAL_POINTER.type);
        }

        if(lighting_enabled) {
            GLfloat contribution[4];
            uint32 to_add;

            for(GLubyte i = 0; i < MAX_LIGHTS; ++i) {
                if(isLightEnabled(i)) {
                    calculateLightingContribution(i, &vertex->x, normal, contribution);
                    to_add = PVR_PACK_COLOR(contribution[0], contribution[1], contribution[2], contribution[3]);

                    /* FIXME: Add the colour to argb */
                }
            }
        }

        ++dst;
    }
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    TRACE();

    if(checkImmediateModeInactive(__func__)) {
        return;
    }

    submitVertices(mode, 0, count, type, indices);
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    TRACE();

    if(checkImmediateModeInactive(__func__)) {
        return;
    }

    submitVertices(mode, first, count, GL_UNSIGNED_SHORT, NULL);
}

void APIENTRY glEnableClientState(GLenum cap) {
    TRACE();

    switch(cap) {
    case GL_VERTEX_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES |= VERTEX_ENABLED_FLAG;
    break;
    case GL_COLOR_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES |= DIFFUSE_ENABLED_FLAG;
    break;
    case GL_NORMAL_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES |= NORMAL_ENABLED_FLAG;
    break;
    case GL_TEXTURE_COORD_ARRAY:
        (ACTIVE_CLIENT_TEXTURE) ?
            (ENABLED_VERTEX_ATTRIBUTES |= ST_ENABLED_FLAG):
            (ENABLED_VERTEX_ATTRIBUTES |= UV_ENABLED_FLAG);
    break;
    default:
        _glKosThrowError(GL_INVALID_ENUM, "glEnableClientState");
    }
}

void APIENTRY glDisableClientState(GLenum cap) {
    TRACE();

    switch(cap) {
    case GL_VERTEX_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES &= ~VERTEX_ENABLED_FLAG;
    break;
    case GL_COLOR_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES &= ~DIFFUSE_ENABLED_FLAG;
    break;
    case GL_NORMAL_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES &= ~NORMAL_ENABLED_FLAG;
    break;
    case GL_TEXTURE_COORD_ARRAY:
        (ACTIVE_CLIENT_TEXTURE) ?
            (ENABLED_VERTEX_ATTRIBUTES &= ~ST_ENABLED_FLAG):
            (ENABLED_VERTEX_ATTRIBUTES &= ~UV_ENABLED_FLAG);
    break;
    default:
        _glKosThrowError(GL_INVALID_ENUM, "glDisableClientState");
    }
}

void APIENTRY glClientActiveTextureARB(GLenum texture) {
    TRACE();

    if(texture < GL_TEXTURE0_ARB || texture > GL_TEXTURE0_ARB + MAX_TEXTURE_UNITS) {
        _glKosThrowError(GL_INVALID_ENUM, "glClientActiveTextureARB");
    }

    if(_glKosHasError()) {
        _glKosPrintError();
        return;
    }

    ACTIVE_CLIENT_TEXTURE = (texture == GL_TEXTURE1_ARB) ? 1 : 0;
}

void APIENTRY glTexCoordPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    AttribPointer* tointer = (ACTIVE_CLIENT_TEXTURE == 0) ? &UV_POINTER : &ST_POINTER;

    tointer->ptr = pointer;
    tointer->stride = stride;
    tointer->type = type;
    tointer->size = size;
}

void APIENTRY glVertexPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    VERTEX_POINTER.ptr = pointer;
    VERTEX_POINTER.stride = stride;
    VERTEX_POINTER.type = type;
    VERTEX_POINTER.size = size;
}

void APIENTRY glColorPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    DIFFUSE_POINTER.ptr = pointer;
    DIFFUSE_POINTER.stride = stride;
    DIFFUSE_POINTER.type = type;
    DIFFUSE_POINTER.size = size;
}

void APIENTRY glNormalPointer(GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    NORMAL_POINTER.ptr = pointer;
    NORMAL_POINTER.stride = stride;
    NORMAL_POINTER.type = type;
    NORMAL_POINTER.size = 3;
}
