#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class camera final
{
	float _camera_speed = 0.25f;
	float _yaw = -90.0f;	// yaw is initialized to -90.0 degrees since a yaw of 0.0 results in a direction vector pointing to the right so we initially rotate a bit to the left.
	float _pitch = 0.0f;
	float _fov = 45.0f;

	glm::vec3 _camera_pos = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 _camera_front = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 _camera_up = glm::vec3(0.0f, 1.0f, 0.0f);

public:

	auto& position() { return _camera_pos; }
	auto& fov() { return _fov; }

	auto projection(float width, float height) { return glm::perspective(glm::radians(_fov), width / height, 0.1f, 100.f); }
	auto view() { return glm::lookAt(_camera_pos, _camera_pos + _camera_front, _camera_up); }

	void move_forward()
	{
		_camera_pos += _camera_speed * _camera_front;
	}

	void move_back()
	{
		_camera_pos -= _camera_speed * _camera_front;
	}

	void strafe_left()
	{
		_camera_pos -= glm::normalize(glm::cross(_camera_front, _camera_up)) * _camera_speed;
	}

	void strafe_right()
	{
		_camera_pos += glm::normalize(glm::cross(_camera_front, _camera_up)) * _camera_speed;
	}

	void fov(float offset)
	{
		_fov += offset;
		if (_fov < 1.0f)
			_fov = 1.0f;
		if (_fov > 45.0f)
			_fov = 45.0f;
	}
};
