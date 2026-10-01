#include "core/config/CorrectionStore.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace qss {
namespace {

constexpr int kFormatVersion = 3;

void SetError(std::string* errorMessage, const std::string& message) {
	if (errorMessage != nullptr) {
		*errorMessage = message;
	}
}

nlohmann::json ToJson(const SharedHandCorrection& value) {
	return {
		{"enabled", value.enabled != 0},
		{"center_offset_enabled", value.centerOffsetEnabled != 0},
		{"inner_deadzone_enabled", value.innerDeadzoneEnabled != 0},
		{"outer_normalization_enabled", value.outerNormalizationEnabled != 0},
		{"clamp_enabled", value.clampEnabled != 0},
		{"center", {value.centerX, value.centerY}},
		{"inner_deadzone", value.innerDeadzone},
		{"outer_scale", value.outerScale},
		{"response_curve", value.responseCurve},
		{"smoothing", value.smoothing},
		{"inner_radius", value.innerRadius},
		{"outer_radius", value.outerRadius},
	};
}

bool IsFiniteInRange(float value, float minimum, float maximum) {
	return std::isfinite(value) && value >= minimum && value <= maximum;
}

SharedHandCorrection FromJson(const nlohmann::json& json) {
	SharedHandCorrection value = MakeDefaultSharedCorrection();
	value.enabled = json.at("enabled").get<bool>() ? 1U : 0U;
	value.centerOffsetEnabled = json.at("center_offset_enabled").get<bool>() ? 1U : 0U;
	value.innerDeadzoneEnabled = json.at("inner_deadzone_enabled").get<bool>() ? 1U : 0U;
	value.outerNormalizationEnabled = json.at("outer_normalization_enabled").get<bool>() ? 1U : 0U;
	value.clampEnabled = json.at("clamp_enabled").get<bool>() ? 1U : 0U;

	const auto& center = json.at("center");
	if (!center.is_array() || center.size() != 2) {
		throw std::runtime_error("center must contain exactly two values");
	}
	value.centerX = center.at(0).get<float>();
	value.centerY = center.at(1).get<float>();
	value.innerDeadzone = json.at("inner_deadzone").get<float>();
	value.outerScale = json.at("outer_scale").get<float>();
	value.responseCurve = json.at("response_curve").get<float>();
	value.smoothing = json.at("smoothing").get<float>();

	const auto& inner = json.at("inner_radius");
	if (!inner.is_array() || inner.size() != kSharedDirectionCount) {
		throw std::runtime_error("inner_radius must contain 360 values");
	}
	for (std::size_t index = 0; index < value.innerRadius.size(); ++index) {
		value.innerRadius[index] = inner.at(index).get<float>();
	}

	const auto& outer = json.at("outer_radius");
	if (!outer.is_array() || outer.size() != kSharedDirectionCount) {
		throw std::runtime_error("outer_radius must contain 360 values");
	}
	for (std::size_t index = 0; index < value.outerRadius.size(); ++index) {
		value.outerRadius[index] = outer.at(index).get<float>();
	}

	if (!IsFiniteInRange(value.centerX, -2.0F, 2.0F) ||
		!IsFiniteInRange(value.centerY, -2.0F, 2.0F) ||
		!IsFiniteInRange(value.innerDeadzone, 0.0F, 0.999F) ||
		!IsFiniteInRange(value.outerScale, 0.25F, 2.0F) ||
		!IsFiniteInRange(value.responseCurve, -1.0F, 1.0F) ||
		!IsFiniteInRange(value.smoothing, 0.0F, 1.0F)) {
		throw std::runtime_error("scalar correction setting is outside the valid range");
	}
	for (const float radius : value.innerRadius) {
		if (!IsFiniteInRange(radius, 0.0F, 0.999F)) {
			throw std::runtime_error("inner radius is outside the valid range");
		}
	}
	for (const float radius : value.outerRadius) {
		if (!IsFiniteInRange(radius, 0.001F, 4.0F)) {
			throw std::runtime_error("outer radius is outside the valid range");
		}
	}
	return value;
}

} // namespace

SharedHandCorrection MakeDefaultSharedCorrection() {
	SharedHandCorrection value;
	value.enabled = 0;
	value.centerOffsetEnabled = 1;
	value.innerDeadzoneEnabled = 1;
	value.outerNormalizationEnabled = 1;
	value.clampEnabled = 1;
	value.innerRadius.fill(0.0F);
	value.outerRadius.fill(1.0F);
	return value;
}

bool SaveCorrectionSettings(
	const std::filesystem::path& path,
	const SharedHandCorrection& left,
	const SharedHandCorrection& right,
	std::string* errorMessage
) {
	std::error_code error;
	if (!path.parent_path().empty()) {
		std::filesystem::create_directories(path.parent_path(), error);
		if (error) {
			SetError(errorMessage, "Failed to create settings directory.");
			return false;
		}
	}

	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream) {
		SetError(errorMessage, "Failed to open calibration.json for writing.");
		return false;
	}

	const nlohmann::json json = {
		{"version", kFormatVersion},
		{"left", ToJson(left)},
		{"right", ToJson(right)},
	};
	stream << json.dump(2) << '\n';
	if (!stream.good()) {
		SetError(errorMessage, "Failed to write calibration.json.");
		return false;
	}
	return true;
}

bool LoadCorrectionSettings(
	const std::filesystem::path& path,
	SharedHandCorrection& left,
	SharedHandCorrection& right,
	std::string* errorMessage
) {
	left = MakeDefaultSharedCorrection();
	right = MakeDefaultSharedCorrection();

	try {
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			SetError(errorMessage, "calibration.json does not exist.");
			return false;
		}

		nlohmann::json json;
		stream >> json;
		if (json.at("version").get<int>() != kFormatVersion) {
			SetError(errorMessage, "Unsupported calibration.json version.");
			return false;
		}

		SharedHandCorrection loadedLeft = FromJson(json.at("left"));
		SharedHandCorrection loadedRight = FromJson(json.at("right"));
		left = loadedLeft;
		right = loadedRight;
		return true;
	} catch (const std::exception& exception) {
		left = MakeDefaultSharedCorrection();
		right = MakeDefaultSharedCorrection();
		SetError(errorMessage, exception.what());
		return false;
	}
}

} // namespace qss
