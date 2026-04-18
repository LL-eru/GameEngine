#include "Glfw.hxx"
#include <stdexcept>

static int s_windowCount = 0;

GLFW::GLFW(int width, int height, const std::string& title)
	: m_width(width), m_height(height), m_window(nullptr)
{
    if (!s_windowCount)
    {
        if (!glfwInit())
            throw std::runtime_error("Failed to init GLFW");

        // Vulkan‘O’ñ‚È‚Ì‚ÅOpenGL–³Œø‰»
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
	s_windowCount++;

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!m_window)
        throw std::runtime_error("Failed to create GLFW window");

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);
}

GLFW::~GLFW()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
		m_window = nullptr;
    }
    s_windowCount--;
    
    if(!s_windowCount)
        glfwTerminate();
}

void GLFW::PollEvents()
{
    glfwPollEvents();
}

bool GLFW::ShouldClose() const
{
    return glfwWindowShouldClose(m_window);
}

GLFWwindow* GLFW::GetNativeHandle() const
{
    return m_window;
}

int GLFW::GetWidth() const { return m_width; }
int GLFW::GetHeight() const { return m_height; }

void GLFW::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    GLFW* self = static_cast<GLFW*>(glfwGetWindowUserPointer(window));
	LOG_INFO("GLFW", "Framebuffer resized: " + std::to_string(width) + "x" + std::to_string(height));
    if (self)
    {
        self->m_width = width;
        self->m_height = height;
    }
}