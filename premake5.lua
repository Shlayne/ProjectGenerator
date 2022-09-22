include "Dependencies/premake/Custom/solutionitems.lua"

workspace "ProjectGenerator"
	architecture "x86_64"
	startproject "ProjectGenerator"

	configurations { "Profile", "Debug", "Release", "Dist" }

	solutionitems {
		-- Visual Studio
		".editorconfig",

		-- Git
		".gitignore",
		".gitattributes",

		-- Scripts
		"Scripts/GenerateProjects.bat",

		-- Lua Scripts
		"premake5.lua",
		"Dependencies/Dependencies.lua",
		"Dependencies/premake/Custom/solutionitems.lua",
		"Dependencies/premake/Custom/usestdpreproc.lua",
		
		-- Misc
		"README.md"
	}

	flags {
		"MultiProcessorCompile"
	}

OutputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

include "Dependencies/premake/Custom/usestdpreproc.lua"
include "Dependencies/Dependencies.lua"

-- Add any projects here with 'include "__PROJECT_NAME__"'
include "ProjectGenerator"
