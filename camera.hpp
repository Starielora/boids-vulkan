#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class camera final
{
	float _speed = 0.25f;
	float _sensitivity = 0.1f;
	float _yaw = -90.0f;
	float _pitch = 0.0f;
	float _fov = 45.0f;

	glm::vec3 _camera_pos = glm::vec3(0.0f, 1.0f, 3.0f);
	glm::vec3 _camera_dir = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 _camera_up = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 _camera_right = glm::vec3();

public:

	auto& position() { return _camera_pos; }
	auto& up() { return _camera_up; }
	auto& front() { return _camera_dir; }
	auto& fov() { return _fov; }
	auto& right() { return _camera_right; }

	auto projection(float width, float height) { return glm::perspective(glm::radians(_fov), width / height, 0.1f, 100.f); }
	auto view() {
		return glm::lookAtLH(_camera_pos + _camera_dir, _camera_pos, _camera_up);
	}

	void move_forward()
	{
		_camera_pos += _speed * _camera_dir;
	}

	void move_back()
	{
		_camera_pos -= _speed * _camera_dir;
	}

	void strafe_left()
	{
		_camera_pos -= glm::normalize(glm::cross(_camera_dir, _camera_up)) * _speed;
	}

	void strafe_right()
	{
		_camera_pos += glm::normalize(glm::cross(_camera_dir, _camera_up)) * _speed;
	}

	void fov(float offset)
	{
		_fov += offset;
		if (_fov < 1.0f)
			_fov = 1.0f;
		if (_fov > 45.0f)
			_fov = 45.0f;
	}

	void look_around(glm::vec2 screen_offset)
	{
		static bool first_mouse = true;
		static glm::vec2 last_mouse_pos{0, 0};

		if (first_mouse)
		{
			last_mouse_pos = screen_offset;
			first_mouse = false;
		}

		auto offset = glm::vec2(screen_offset.x - last_mouse_pos.x, last_mouse_pos.y - screen_offset.y);
		last_mouse_pos = screen_offset;

		offset *= _sensitivity;

		_yaw += offset.x;
		_pitch += offset.y;

		if (_pitch > 89.0f)
			_pitch = 89.0f;
		if (_pitch < -89.0f)
			_pitch = -89.0f;

		glm::vec3 direction;
		direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
		direction.y = sin(glm::radians(_pitch));
		direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));
		_camera_dir = glm::normalize(direction);
		_camera_right = glm::normalize(glm::cross(_camera_dir, {0, 1, 0}));
		_camera_up = glm::normalize(glm::cross(_camera_right, _camera_dir));
	}
};
