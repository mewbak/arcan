/*
 * Copyright 2014, Björn Ståhl
 * License: 3-Clause BSD, see COPYING file in arcan source repository.
 * Reference: http://arcan-fe.com
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>

#include "glfun.h"

#include "../video_platform.h"
#include "../platform.h"

#include "arcan_math.h"
#include "arcan_general.h"
#include "arcan_video.h"
#include "arcan_videoint.h"

static const char* defvprg =
"#version 120\n"
"uniform mat4 modelview;\n"
"uniform mat4 projection;\n"

"attribute vec2 texcoord;\n"
"varying vec2 texco;\n"
"attribute vec4 vertex;\n"
"void main(){\n"
"	gl_Position = (projection * modelview) * vertex;\n"
"   texco = texcoord;\n"
"}";

const char* deffprg =
"#version 120\n"
"uniform sampler2D map_diffuse;\n"
"varying vec2 texco;\n"
"uniform float obj_opacity;\n"
"void main(){\n"
"   vec4 col = texture2D(map_diffuse, texco);\n"
"   col.a = col.a * obj_opacity;\n"
"	gl_FragColor = col;\n"
"}";

const char* defcfprg =
"#version 120\n"
"varying vec2 texco;\n"
"uniform vec3 obj_col;\n"
"uniform float obj_opacity;\n"
"void main(){\n"
"   gl_FragColor = vec4(obj_col.rgb, obj_opacity);\n"
"}\n";

const char * defcvprg =
"#version 120\n"
"uniform mat4 modelview;\n"
"uniform mat4 projection;\n"
"attribute vec4 vertex;\n"
"void main(){\n"
" gl_Position = (projection * modelview) * vertex;\n"
"}";

arcan_shader_id agp_default_shader(enum SHADER_TYPES type)
{
	static arcan_shader_id shids[SHADER_TYPE_ENDM];
	static bool defshdr_build;

	assert(type < SHADER_TYPE_ENDM);

	if (!defshdr_build){
		shids[0] = arcan_shader_build("DEFAULT", NULL, defvprg, deffprg);
		shids[1] = arcan_shader_build("DEFAULT_COLOR", NULL, defcvprg, defcfprg);
		defshdr_build = true;
	}

	return shids[type];
}

const char* agp_ident()
{
	return "OPENGL21";
}

void agp_shader_source(enum SHADER_TYPES type,
	const char** vert, const char** frag)
{
	switch(type){
		case BASIC_2D:
			*vert = defvprg;
			*frag = deffprg;
		break;

		case COLOR_2D:
			*vert = defcvprg;
			*frag = defcfprg;
		break;

		default:
			*vert = NULL;
			*frag = NULL;
		break;
	}
}

const char** agp_envopts()
{
	static const char* env[] = {
		NULL
	};
	return env;
}

const char* agp_shader_language()
{
	return "GLSL120";
}

void agp_readback_synchronous(struct storage_info_t* dst)
{
	if (!dst->txmapped == TXSTATE_TEX2D || !dst->vinf.text.raw)
		return;

	glBindTexture(GL_TEXTURE_2D, dst->vinf.text.glid);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_PIXEL_FORMAT,
		GL_UNSIGNED_BYTE, dst->vinf.text.raw);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void agp_drop_vstore(struct storage_info_t* s)
{
	if (!s)
		return;

	glDeleteTextures(1, &s->vinf.text.glid);
	s->vinf.text.glid = 0;

	if (GL_NONE != s->vinf.text.rid)
		glDeleteBuffers(1, &s->vinf.text.rid);

	if (GL_NONE != s->vinf.text.wid)
		glDeleteBuffers(1, &s->vinf.text.wid);

	memset(s, '\0', sizeof(struct storage_info_t));
}

static void pbo_stream(struct storage_info_t* s, av_pixel* buf)
{
	agp_activate_vstore(s);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s->vinf.text.wid);
	size_t ntc = s->w * s->h;

	av_pixel* ptr = (av_pixel*) glMapBuffer(GL_PIXEL_UNPACK_BUFFER,
			GL_WRITE_ONLY);

	if (!ptr)
		return;

/* note, explicitly replace with a simd unaligned version */
 	if ( ((uintptr_t)ptr % 16) == 0 && ((uintptr_t)buf % 16) == 0	)
		memcpy(ptr, buf, ntc * GL_PIXEL_BPP);
	else
		for (size_t i = 0; i < ntc; i++)
			*ptr++ = *buf++;

	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->w, s->h,
			GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, 0);

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	agp_deactivate_vstore();
}

static inline void setup_unpack_pbo(struct storage_info_t* s, void* buf)
{
	agp_activate_vstore(s);
	glGenBuffers(1, &s->vinf.text.wid);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s->vinf.text.wid);
		glBufferData(GL_PIXEL_UNPACK_BUFFER,
			s->w * s->h * GL_PIXEL_BPP, buf, GL_STREAM_DRAW);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	agp_deactivate_vstore();
}

struct stream_meta agp_stream_prepare(struct storage_info_t* s,
		struct stream_meta meta, enum stream_type type)
{
	struct stream_meta res = meta;
	res.state = true;

	switch (type){
	case STREAM_RAW:
		if (!s->vinf.text.wid)
			setup_unpack_pbo(s, NULL);

		if (!s->vinf.text.raw){
			s->vinf.text.s_raw = s->w * s->h * GL_PIXEL_BPP;
			s->vinf.text.raw = arcan_alloc_mem(s->vinf.text.s_raw,
				ARCAN_MEM_VBUFFER, ARCAN_MEM_BZERO, ARCAN_MEMALIGN_PAGE);
		}

		res.buf = s->vinf.text.raw;
		res.state = res.buf != NULL;
	break;

	case STREAM_RAW_DIRECT:
		if (!s->vinf.text.wid){
			setup_unpack_pbo(s, meta.buf);
		}
		else
			pbo_stream(s, meta.buf);
	break;

	case STREAM_RAW_DIRECT_SYNCHRONOUS:
		agp_activate_vstore(s);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->w, s->h,
			GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, meta.buf);
		agp_deactivate_vstore();
	break;

	case STREAM_HANDLE:
/* if platform_video_map_handle fails here, prepare an
 * empty vstore and attempt again, if that succeeds it
 * means that we had to go through a RTT indirection,
 * if that fails we should convey back to the client that
  we can't accept this kind of transfer */
		res.state = platform_video_map_handle(s, meta.handle);
	break;
	}

	return res;
}

void agp_stream_release(struct storage_info_t* s)
{
	pbo_stream(s, s->vinf.text.raw);
	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GL_NONE);
}

void agp_stream_commit(struct storage_info_t* s)
{
}

static void pbo_alloc_read(struct storage_info_t* store)
{
	GLuint pboid;
	glGenBuffers(1, &pboid);
	store->vinf.text.rid = pboid;

	glBindBuffer(GL_PIXEL_PACK_BUFFER, pboid);
	glBufferData(GL_PIXEL_PACK_BUFFER,
		store->w * store->h * store->bpp, NULL, GL_STREAM_READ);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

static void pbo_alloc_write(struct storage_info_t* store)
{
	GLuint pboid;
	glGenBuffers(1, &pboid);
	store->vinf.text.wid = pboid;

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboid);
	glBufferData(GL_PIXEL_UNPACK_BUFFER,
		store->w * store->h * store->bpp, NULL, GL_STREAM_READ);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static void default_release(void* tag)
{
	if (!tag)
		return;

	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void agp_resize_vstore(struct storage_info_t* s, size_t w, size_t h)
{
	s->w = w;
	s->h = h;
	s->bpp = GL_PIXEL_BPP;

	if (s->vinf.text.raw){
		arcan_mem_free(s->vinf.text.raw);
		s->vinf.text.s_raw = w * h * s->bpp;
		s->vinf.text.raw = arcan_alloc_mem(s->vinf.text.s_raw,
			ARCAN_MEM_VBUFFER, ARCAN_MEM_BZERO, ARCAN_MEMALIGN_PAGE);
	}

/* Note: should we handle the fact where we have a pre-existing
 * raw and scale into the new size? Limited uses.. */

	if (s->vinf.text.wid){
		glDeleteBuffers(1, &s->vinf.text.wid);
		pbo_alloc_write(s);
	}

	if (s->vinf.text.rid){
		glDeleteBuffers(1, &s->vinf.text.rid);
		pbo_alloc_read(s);
	}

	agp_update_vstore(s, true);
}

void agp_request_readback(struct storage_info_t* store)
{
	if (!store || store->txmapped != TXSTATE_TEX2D)
		return;

	if (!store->vinf.text.rid)
		pbo_alloc_read(store);

	glBindTexture(GL_TEXTURE_2D, store->vinf.text.glid);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, store->vinf.text.rid);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_PIXEL_FORMAT,
			GL_UNSIGNED_BYTE, NULL);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

struct asynch_readback_meta agp_poll_readback(struct storage_info_t* store)
{
	struct asynch_readback_meta res = {
	.release = default_release
	};

	if (!store || store->txmapped != TXSTATE_TEX2D ||
		store->vinf.text.rid == GL_NONE)
		return res;

	glBindBuffer(GL_PIXEL_PACK_BUFFER, store->vinf.text.rid);

	res.w = store->w;
	res.h = store->h;
	res.tag = (void*) 0xdeadbeef;
	res.ptr = (av_pixel*) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_WRITE);

	return res;
}