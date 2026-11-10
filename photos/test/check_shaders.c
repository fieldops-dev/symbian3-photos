#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* Load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = calloc(n + 256, 1);
    fread(text, 1, n, f);
    fclose(f);

    const char* lines[] = { "#ifdef GL_FRAGMENT_PRECISION_HIGH", "#define HP", "#else", "#endif", "precision " };
    for (char* line = text; *line;)
    {
        char* end = strchr(line, '\n');
        for (unsigned d = 0; d < sizeof(lines) / sizeof(lines[0]); d++)
            if (strncmp(line, lines[d], strlen(lines[d])) == 0)
                memset(line, ' ', end ? (size_t)(end - line) : strlen(line));
        if (!end)
            break;
        line = end + 1;
    }
    const char* qualifiers[] = { "highp ", "mediump ", "lowp " };
    for (unsigned d = 0; d < sizeof(qualifiers) / sizeof(qualifiers[0]); d++)
        for (char* p; (p = strstr(text, qualifiers[d]));)
            memset(p, ' ', strlen(qualifiers[d]));
    return text;
}

static int Compile(GLenum type, const char* path, GLuint* shader)
{
    char* text = Load(path);
    if (!text)
        return printf("%s: cannot read\n", path), 0;
    const char* sources[2] = { "#version 120\n#define HP\n", text };
    *shader = glCreateShader(type);
    glShaderSource(*shader, 2, sources, NULL);
    glCompileShader(*shader);
    GLint ok = 0;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        glGetShaderInfoLog(*shader, sizeof(log), NULL, log);
        printf("%s: %s\n", path, log);
    }
    free(text);
    return ok;
}

int main(int argc, char** argv)
{
    CGLPixelFormatAttribute attributes[] = { kCGLPFAAccelerated, 0 };
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    if (CGLChoosePixelFormat(attributes, &format, &count) || CGLCreateContext(format, NULL, &context) ||
        CGLSetCurrentContext(context))
        return printf("no OpenGL context\n"), 2;
    int failures = 0;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        GLuint vs, fs;
        int ok = Compile(GL_VERTEX_SHADER, argv[i], &vs) & Compile(GL_FRAGMENT_SHADER, argv[i + 1], &fs);
        if (ok)
        {
            GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            GLint linked = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked)
            {
                printf("%s + %s: link failed\n", argv[i], argv[i + 1]);
                ok = 0;
            }
        }
        printf("%s %s + %s\n", ok ? "ok  " : "FAIL", argv[i], argv[i + 1]);
        failures += !ok;
    }
    return failures ? 1 : 0;
}
