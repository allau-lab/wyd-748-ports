#include "d3d9_gles_bridge.h"

#include <GLES2/gl2.h>
#include <cstdio>

namespace
{
GLuint Compile(GLenum type, const char* src)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, nullptr);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[512]{};
		glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
		std::fprintf(stderr, "[WYD_SWITCH][gles] shader: %s\n", log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}
} // namespace

bool WYD_D3D9GlesCreate(WYD_D3D9GlesBridge& out)
{
	out = {};
	static const char* vs =
		"attribute vec2 a_position;\n"
		"attribute vec2 a_uv;\n"
		"varying vec2 v_uv;\n"
		"void main(){ gl_Position=vec4(a_position,0.0,1.0); v_uv=a_uv; }\n";
	static const char* fs =
		"precision mediump float;\n"
		"uniform sampler2D u_texture;\n"
		"varying vec2 v_uv;\n"
		"void main(){ gl_FragColor=texture2D(u_texture,v_uv); }\n";

	GLuint v = Compile(GL_VERTEX_SHADER, vs);
	GLuint f = Compile(GL_FRAGMENT_SHADER, fs);
	if (!v || !f)
		return false;

	out.program = glCreateProgram();
	glAttachShader(out.program, v);
	glAttachShader(out.program, f);
	glBindAttribLocation(out.program, 0, "a_position");
	glBindAttribLocation(out.program, 1, "a_uv");
	glLinkProgram(out.program);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint linked = 0;
	glGetProgramiv(out.program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		std::fprintf(stderr, "[WYD_SWITCH][gles] link falhou\n");
		return false;
	}
	out.attr_pos = 0;
	out.attr_uv = 1;
	out.uni_tex = glGetUniformLocation(out.program, "u_texture");

	const uint8_t pixels[] = {
		255, 30, 30, 255, 30, 255, 30, 255,
		30, 30, 255, 255, 255, 255, 30, 255};
	glGenTextures(1, &out.texture);
	glBindTexture(GL_TEXTURE_2D, out.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	const float quad[] = {
		-0.6f, -0.6f, 0.f, 1.f,
		0.6f, -0.6f, 1.f, 1.f,
		-0.6f, 0.6f, 0.f, 0.f,
		0.6f, 0.6f, 1.f, 0.f,
	};
	glGenBuffers(1, &out.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	out.ready = (glGetError() == GL_NO_ERROR);
	std::fprintf(stderr, "[WYD_SWITCH][gles] bridge %s\n", out.ready ? "OK" : "FAIL");
	return out.ready;
}

void WYD_D3D9GlesDestroy(WYD_D3D9GlesBridge& b)
{
	if (b.vbo)
		glDeleteBuffers(1, &b.vbo);
	if (b.texture)
		glDeleteTextures(1, &b.texture);
	if (b.program)
		glDeleteProgram(b.program);
	b = {};
}

bool WYD_D3D9GlesClear(WYD_D3D9GlesBridge& b, uint32_t argb)
{
	if (!b.ready)
		return false;
	b.clear_argb = argb;
	const float a = ((argb >> 24) & 255) / 255.f;
	const float r = ((argb >> 16) & 255) / 255.f;
	const float g = ((argb >> 8) & 255) / 255.f;
	const float bl = (argb & 255) / 255.f;
	glClearColor(r, g, bl, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return true;
}

bool WYD_D3D9GlesDrawTestQuad(WYD_D3D9GlesBridge& b)
{
	if (!b.ready)
		return false;
	glUseProgram(b.program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, b.texture);
	glUniform1i(b.uni_tex, 0);
	glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	return glGetError() == GL_NO_ERROR;
}
