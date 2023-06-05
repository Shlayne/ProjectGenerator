#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>

//#define PRINT_ONLY

#if	PRINT_ONLY
	#define RUN_COMMAND(x) (static_cast<bool>(std::cout << "Command: \"" << x << "\"\n") && false)
#elif CONFIG_DEBUG || CONFIG_RELEASE
	#define RUN_COMMAND(x) ((static_cast<bool>(std::cout << "Command: \"" << x << "\"\n") || true) && system(x))
#else
	#define RUN_COMMAND(x) (system(x))
#endif

#if SYSTEM_WINDOWS
	#define EMPTY_FILE "nul"
	#define START_PROCESS_BEGIN "start "
	#define START_PROCESS_END
	#define DEFAULT_PROJECT_DIRECTORY "C:\\Workspace\\Programming\\Dev\\C++"
#else
	#define EMPTY_FILE "/dev/null"
	#define START_PROCESS_BEGIN
	#define START_PROCESS_END " &"
	#define DEFAULT_PROJECT_DIRECTORY "/"
#endif

using ReturnCode = uint8_t;
enum : ReturnCode
{
	ReturnCode_Success = 0,
	ReturnCode_ShowHelpMessage,
	ReturnCode_GitMissing,
	ReturnCode_GitHubCLIMissing,
	ReturnCode_ProjectDirectoryAlreadyExists,
	ReturnCode_CouldntCreateDirectory,
	ReturnCode_CouldntCreateRepository,
	ReturnCode_CouldntCloneRepository,
	ReturnCode_CouldntReadFile,
	ReturnCode_CouldntWriteFile,
	ReturnCode_CouldntCommitToRepository,
	ReturnCode_CouldntGenerateProjects,
	ReturnCode_CouldntOpenVSSolution,
};

ReturnCode Run(int argc, const char* const argv[]);

int main(int argc, char* argv[])
{
	ReturnCode rc{Run(argc, argv)};
	int irc{static_cast<int>(rc)};
	if (rc != ReturnCode_Success)
	{
		if (rc == ReturnCode_ShowHelpMessage)
		{
			rc = ReturnCode_Success;
			std::cout << "Usage: " << argv[0] << " [<ProjectName> [options] | --help]\n\n";
			std::cout << "Options:\n";
			std::cout << "   --dir filepath   Set the local directory of the project (default is " DEFAULT_PROJECT_DIRECTORY ")\n";
			std::cout << "   --public         Make the project's repository public (default is private)\n";
			std::cout << "   --olc            Use OLCTemplate instead of ProjectTemplate\n";
			std::cout << '\n';
		}
		else
		{
			constexpr const char* errorMessages[]
			{
				"Must have git installed. Get it here: https://git-scm.com/downloads/",
				"Must have GitHub CLI installed. Get it here: https://cli.github.com/",
				"A file already exists at the project directory.",
				"Couldn't create project directory.",
				"Couldn't create repository.",
				"Couldn't clone repository.",
				"Couldn't read file.",
				"Couldn't write file.",
				"Couldn't commit generated changes to the repository.",
				"Couldn't generate projects.",
				"Couldn't open Visual Studio solution.",
			};
			std::cerr << errorMessages[irc - 2] << '\n';
		}
	}
	return irc;
}

static constexpr std::string_view olcName{"OLCTemplate"};
static constexpr std::string_view prjName{"__PROJECT_NAME__"};
static constexpr std::string_view wksName{"__WORKSPACE_NAME__"};
static constexpr std::string_view pjtName{"ProjectTemplate"};

ReturnCode EditFile(const std::filesystem::path& filepath, const std::function<void(std::string&)>& func);
void ReplaceOLCTemplateWithProjectName(std::string& file, std::string_view arg1);
ReturnCode ReplaceOLCTemplateWithProjectNameInFiles(const std::vector<std::filesystem::path>& filepaths, std::string_view arg1);

ReturnCode Run(int argc, const char* const argv[])
{
	if (argc == 1 || (argc > 1 && std::string_view{"--help"} == argv[1]))
		return ReturnCode_ShowHelpMessage;
	else if (argc > 1)
	{
		if (RUN_COMMAND("git --help > " EMPTY_FILE " 2>&1"))
			return ReturnCode_GitMissing;
		if (RUN_COMMAND("gh > " EMPTY_FILE " 2>&1"))
			return ReturnCode_GitHubCLIMissing;

		std::filesystem::path directory{DEFAULT_PROJECT_DIRECTORY};
		bool isPublic{};
		bool useOLCTemplate{};

		for (int argi{2}; argi < argc; )
		{
			std::string_view arg{argv[argi++]};
			if (arg == "--dir")
				directory = argv[argi++];
			else if (arg == "--public")
				isPublic = true;
			else if (arg == "--olc")
				useOLCTemplate = true;
			else
				return ReturnCode_ShowHelpMessage;
		}

		std::filesystem::path projectDirectory{directory / argv[1]};

		std::error_code error;
		if (std::filesystem::exists(projectDirectory, error) || error)
			return ReturnCode_ProjectDirectoryAlreadyExists;

		if (!std::filesystem::create_directories(projectDirectory, error) || error)
			return ReturnCode_CouldntCreateDirectory;

		// Create the repository.
		std::string command = "gh repo create \"";
		command.append(argv[1]).append("\" --");
		command.append(isPublic ? "public" : "private");
		command.append(" --template Shlayne/");
		command.append(useOLCTemplate ? "OLCTemplate" : "ProjectTemplate");
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode_CouldntCreateRepository;

		// Github sometimes clones the repository as empty.
		// Try to decrease the probability of that I guess?
		std::this_thread::sleep_for(std::chrono::seconds{1});

		// Clone the repository.
		command = "gh repo clone \"";
		command.append(argv[1]).append(1, '"');
		if (!directory.empty())
			command.append(" \"").append(projectDirectory.string()).append(1, '"');
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode_CouldntCloneRepository;

		std::filesystem::path projectProjectDirectory{projectDirectory / argv[1]};

		// Rename the vs solution's project folder to "<dir>/<ProjectName>/<ProjectName>".
		std::filesystem::rename(projectDirectory / (useOLCTemplate ? olcName : prjName), projectProjectDirectory, error);

		ReturnCode rc{ReturnCode_Success};

		// Replace the first "__PROJECT_NAME__" in "<dir>/<ProjectName>/<ProjectName>/premake5.lua" with "<ProjectName>".
		rc = EditFile(projectProjectDirectory / "premake5.lua",
		[argv, useOLCTemplate](std::string& file)
		{
			std::string_view name{useOLCTemplate ? olcName : prjName};
			file.replace(file.find(name), name.size(), argv[1]);
		});
		if (rc != ReturnCode_Success)
			return rc;

		rc = EditFile(projectDirectory / "premake5.lua",
		[argv, useOLCTemplate](std::string& file)
		{
			if (useOLCTemplate)
				ReplaceOLCTemplateWithProjectName(file, argv[1]);
			else
			{
				// Replace the first and last "__PROJECT_NAME__" in "<dir>/<ProjectName>/premake5.lua" with "<ProjectName>"
				// and "__WORKSPACE_NAME__" with "<ProjectName>".
				file.replace(file.find(wksName), wksName.size(), argv[1]);
				file.replace(file.find(prjName), prjName.size(), argv[1]);
				file.replace(file.rfind(prjName), prjName.size(), argv[1]);
			}
		});
		if (rc != ReturnCode_Success)
			return rc;

		if (useOLCTemplate)
		{
			std::filesystem::path projectProjectSrcDirectory{projectProjectDirectory / "src"};
			std::filesystem::path projectProjectSrcOLCTemplateH{projectProjectSrcDirectory / "OLCTemplate.h"};
			std::filesystem::path projectProjectSrcOLCTemplateCPP{projectProjectSrcDirectory / "OLCTemplate.cpp"};
			rc = ReplaceOLCTemplateWithProjectNameInFiles(
			{
				projectDirectory / "Dependencies/Dependencies.lua",
				projectDirectory / "README.md",
				projectProjectSrcOLCTemplateH,
				projectProjectSrcOLCTemplateCPP,
				projectProjectSrcDirectory / "main.cpp"

			}, argv[1]);
			if (rc != ReturnCode_Success)
				return rc;

			std::filesystem::path destName{projectProjectSrcDirectory / argv[1]};
			std::filesystem::rename(projectProjectSrcOLCTemplateH, destName.replace_extension("h"), error);
			std::filesystem::rename(projectProjectSrcOLCTemplateCPP, destName.replace_extension("cpp"), error);
		}
		else
		{
			rc = EditFile(projectDirectory / "README.md",
			[argv](std::string& file)
			{
				file.replace(file.find(pjtName), pjtName.size(), argv[1]);
			});
			if (rc != ReturnCode_Success)
				return rc;
		}

		std::filesystem::current_path(projectDirectory, error);

		// Commit the generated changes to the project's repository.
		command = "git add -A && git commit -m \"Project Generation Commit.\" && git push";
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode_CouldntCommitToRepository;

		std::filesystem::current_path(projectDirectory / L"Scripts", error);
		// Call GenerateProjects.bat
		command = "GenerateProjects.bat";
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode_CouldntGenerateProjects;

		// Open visual studio solution as a new process.
		command = START_PROCESS_BEGIN "\"\" \"";
		command.append(projectDirectory.string());
		command.append(1, std::filesystem::path::preferred_separator);
		command.append(argv[1]);
		command.append(".sln\"" START_PROCESS_END);
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode_CouldntOpenVSSolution;
	}
	else
		return ReturnCode_ShowHelpMessage;

	return ReturnCode_Success;
}

ReturnCode EditFile(const std::filesystem::path& filepath, const std::function<void(std::string&)>& func)
{
	std::ifstream inFile{filepath};
	if (!inFile.is_open())
		return ReturnCode_CouldntReadFile;

	std::stringstream inFileStream;
	inFileStream << inFile.rdbuf();
	inFile.close();

	std::string file{inFileStream.str()};
	func(file);

	// Write binary data to not change line endings.
	std::ofstream outFile{filepath, std::ofstream::binary};
	if (!outFile.is_open())
		return ReturnCode_CouldntWriteFile;

	outFile << file;
	outFile.close();

	return ReturnCode_Success;
}

void ReplaceOLCTemplateWithProjectName(std::string& file, std::string_view arg1)
{
	size_t offset{};
	while (true) // This is completely fine, stop complaining.
	{
		offset = file.find(olcName, offset);
		if (offset == std::string::npos)
			break;
		file.replace(offset, olcName.size(), arg1.data());
		offset += arg1.size();
	}
}

ReturnCode ReplaceOLCTemplateWithProjectNameInFiles(const std::vector<std::filesystem::path>& filepaths, std::string_view arg1)
{
	ReturnCode rc{ReturnCode_Success};
	for (size_t i{}; i < filepaths.size() && rc == ReturnCode_Success; ++i)
	{
		rc = EditFile(filepaths[i],
		[arg1](std::string& file)
		{
			ReplaceOLCTemplateWithProjectName(file, arg1);
		});
	}
	return rc;
}
