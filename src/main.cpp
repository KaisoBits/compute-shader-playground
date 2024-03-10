#include <glad/glad.h>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;

constexpr int threshold = 254;
constexpr int workItemsX = 32;
constexpr int workItemsY = 32;

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cout << "Specify image as the command line parameter\n";
		return -1;
	}

	// Initialize GLFW
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW\n";
		return -2;
	}

	// Set GLFW to not create a graphics window
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	// Create a windowed mode window and its OpenGL context
	GLFWwindow* window = glfwCreateWindow(640, 480, "Compute Shader Example", NULL, NULL);
	if (!window)
	{
		std::cerr << "Failed to create GLFW window\n";
		glfwTerminate();
		return -3;
	}

	// Make the window's context current
	glfwMakeContextCurrent(window);

	// Initialize GLAD
	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
	{
		std::cerr << "Failed to initialize GLAD\n";
		return -4;
	}

	stbi_set_flip_vertically_on_load(true);

	int width, height;
	int channelsCount = 4;
	unsigned char* imageData = stbi_load(argv[1], &width, &height, nullptr, STBI_rgb_alpha);

	if (!imageData)
	{
		std::cout << "Failed to load the texture\n";
		return -5;
	}

	unsigned int texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	// Bind the texture to the image unit 0 for the compute shader

	if (channelsCount == 4)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData);
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, imageData);
	}

	glGenerateMipmap(GL_TEXTURE_2D);

	// Compute shader source code
	std::ostringstream shaderSourceStream;
	shaderSourceStream << R"glsl(
#version 430 core
layout (local_size_x = )glsl" << workItemsX << R"glsl(, local_size_y = )glsl" << workItemsY << R"glsl() in;
layout(rgba8, binding = 0) uniform image2D img;

layout (std430, binding = 0) buffer SSBO {
    int sum;
};

const float t = )glsl" << threshold << R"glsl( / 255.0;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(img);
    if (pos.x < size.x && pos.y < size.y)
    {
        vec4 color = imageLoad(img, pos);
        if (color.r >= t)
            atomicAdd(sum, 1);
        if (color.g >= t)
            atomicAdd(sum, 1);
        if (color.b >= t)
            atomicAdd(sum, 1);
    }
}
)glsl";

	const std::string computeShaderSource = shaderSourceStream.str();
	const char* s = computeShaderSource.c_str();

	// Create and compile the compute shader
	GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
	glShaderSource(computeShader, 1, &s, nullptr);
	glCompileShader(computeShader);

	// Check for shader compile errors
	int success;
	char infoLog[512];
	glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(computeShader, 512, nullptr, infoLog);
		std::cerr << "Shader compilation error: " << infoLog << '\n';
		return -6;
	}

	// Create a shader program and attach the compute shader
	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, computeShader);
	glLinkProgram(shaderProgram);

	// Check for linking errors
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
		std::cerr << "Shader linking error: " << infoLog << '\n';
		return -7;
	}

	auto s0 = high_resolution_clock::now();
	GLuint ssbo;
	glGenBuffers(1, &ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
	auto t0 = duration_cast<microseconds>(high_resolution_clock::now() - s0);
	std::cout << "It took " << t0.count() << " microseconds to allocate buffer for the compute shader\n";

	glUseProgram(shaderProgram);

	auto s1 = high_resolution_clock::now();
	glDispatchCompute(static_cast<unsigned int>(ceil(static_cast<float>(width) / workItemsX)),
		static_cast<unsigned int>(ceil(static_cast<float>(height) / workItemsY)),
		1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); // Ensure memory writes are finished
	auto t1 = duration_cast<microseconds>(high_resolution_clock::now() - s1);
	glFinish();
	std::cout << "It took " << t1.count() << " microseconds to execute the compute shader\n";

	auto s2 = high_resolution_clock::now();
	unsigned int gpuResult1;
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(unsigned int), &gpuResult1);
	auto t2 = duration_cast<microseconds>(high_resolution_clock::now() - s2);
	std::cout << t2.count() << " microseconds to copy data back to the CPU\n";

	auto s1t = high_resolution_clock::now();
	glDispatchCompute(static_cast<unsigned int>(ceil(static_cast<float>(width) / workItemsX)),
		static_cast<unsigned int>(ceil(static_cast<float>(height) / workItemsY)),
		1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); // Ensure memory writes are finished
	glFinish();
	auto t1t = duration_cast<microseconds>(high_resolution_clock::now() - s1t);
	std::cout << "It took " << t1t.count() << " microseconds to execute the compute shader (2nd pass)\n";

	auto s2t = high_resolution_clock::now();
	unsigned int gpuResult2;
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(unsigned int), &gpuResult2);
	auto t2t = duration_cast<microseconds>(high_resolution_clock::now() - s2t);
	std::cout << t2t.count() << " microseconds to copy data back to the CPU (2nd pass)\n";

	auto s3 = high_resolution_clock::now();
	unsigned long long cpuResult = 0;
	unsigned long long dataSize = width * height * channelsCount;
	for (unsigned long long i = 0; i < dataSize; i += channelsCount)
	{
		if (imageData[i] >= threshold)
			++cpuResult;
		if (imageData[i + 1] >= threshold)
			++cpuResult;
		if (imageData[i + 2] >= threshold)
			++cpuResult;
	}
	auto t3 = duration_cast<microseconds>(high_resolution_clock::now() - s3);

	std::cout << "It took " << t3.count() << " microseconds to execute the same operation on the CPU\n";

	glfwTerminate();

	std::cout << "GPU counted " << gpuResult1 << " subpixels with value above " << threshold << '\n';
	std::cout << "CPU counted " << cpuResult << " subpixels with value above " << threshold << '\n';

	return 0;
}
