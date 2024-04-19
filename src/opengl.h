/* ―――――――――――――――――――――――――――――――――――◆――――――――――――――――――――――――――――――――――――
   $File: $
   $Date: $
   $Revision: $
   $Creator: Sung Woo Lee $
   $Notice: (C) Copyright 2024 by Sung Woo Lee. All Rights Reserved. $
   ―――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――― */

#define GL_EXTENSION_LIST           \
    X(GL_EXT_texture_sRGB)          \
    X(GL_EXT_framebuffer_sRGB)

#define X(ext) ext,
enum GL_Extensions {
    GL_EXTENSION_LIST
    GL_EXT_COUNT
};
#undef X

struct GL_Info {
    b32 modern;
    char *vendor;
    char *renderer;
    char *version;
    char *shading_language_version;
    char *extensions;
    s32 major;
    s32 minor;

    b32 has_ext[GL_EXT_COUNT];
};


struct GL {
    s32     program;

    s32     transform_id;
    s32     texture_sampler_id;

    GLuint  vao;
    GLuint  vbo;

    Bitmap  white_bitmap;
    u32     white[64][64];
};
