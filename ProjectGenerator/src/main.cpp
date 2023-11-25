#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>

#define PRINT_ONLY 0

#if	PRINT_ONLY
	#define RUN_COMMAND(x) (static_cast<bool>(std::cout << "Command: \"" << x << "\"\n") && false)
#elif CONFIG_DEBUG || CONFIG_RELEASE
	#define RUN_COMMAND(x) ((static_cast<bool>(std::cout << "Command: \"" << x << "\"\n") || true) && system(x))
#else
	#define RUN_COMMAND(x) (system(x))
#endif

#define GITHUB_ACCOUNT_NAME "Shlayne"

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

enum ReturnCode : int8_t
{
	// Zero means everything worked as expected by the program.

	Success = 0,

	// Positive means nothing went wrong, just indicates what happened in general.

	ShowHelpMessage,

	// Negative means something did go wrong.

	GitMissing = -0x80, // start at most negative so auto enum numbers will count up for me while still always being negative.
	GitHubCLIMissing,
	DirectoryArgMissing,
	ProjectDirectoryAlreadyExists,
	CouldntCreateDirectory,
	CouldntCreateRepository,
	CouldntCloneRepository,
	CouldntRenameFile,
	CouldntReadFile,
	CouldntWriteFile,
	CouldntCommitToRepository,
	CouldntGenerateProjects,
	CouldntOpenVSSolution,
};

ReturnCode Run(int argc, const char* const argv[]);

int main(int argc, char* argv[])
{
	ReturnCode rc = Run(argc, argv);
	if (rc != ReturnCode::Success)
	{
		if (rc == ReturnCode::ShowHelpMessage)
		{
			rc = ReturnCode::Success;
			std::cout << "Usage: " << argv[0] << " [<ProjectName> [options] | --help]\n\n";
			std::cout << "Options:\n";
			std::cout << "   --dir filepath   Set the local directory of the project (default is " DEFAULT_PROJECT_DIRECTORY ")\n";
			std::cout << "   --public         Make the project's repository public (default is private)\n";
			std::cout << "   --olc            Use OLCTemplate instead of ProjectTemplate\n";
			std::cout << '\n';
		}
		else if (rc < 0) // Something went wrong...
		{
			constexpr const char* errorMessages[]
			{
				"Must have git installed. Get it here: https://git-scm.com/downloads/",
				"Must have GitHub CLI installed. Get it here: https://cli.github.com/",
				"Missing argument for --dir.",
				"A file already exists at the project directory.",
				"Couldn't create project directory.",
				"Couldn't create repository.",
				"Couldn't clone repository.",
				"Couldn't rename file.",
				"Couldn't read file.",
				"Couldn't write file.",
				"Couldn't commit generated changes to the repository.",
				"Couldn't generate projects.",
				"Couldn't open Visual Studio solution.",
			};
			std::cerr << errorMessages[rc - GitMissing] << '\n';
		}
	}
	return rc;
}

static constexpr std::string_view prjName = "__PROJECT_NAME__";
static constexpr std::string_view wksName = "__WORKSPACE_NAME__";
static constexpr std::string_view pjtName = "ProjectTemplate";
static constexpr std::string_view olcName = "OLCTemplate";

ReturnCode EditFile(const std::filesystem::path& filepath, const std::function<void(std::string&)>& func);
void ReplaceAll(std::string& file, std::string_view replacee, std::string_view replacer);
ReturnCode ReplaceOLCTemplateWithProjectNameInFiles(const std::vector<std::filesystem::path>& filepaths, std::string_view arg1);

ReturnCode Run(int argc, const char* const argv[])
{
	if (argc == 1 || (argc > 1 && std::string_view("--help") == argv[1]))
		return ReturnCode::ShowHelpMessage;
	else if (argc > 1)
	{
		if (RUN_COMMAND("git --help > " EMPTY_FILE " 2>&1"))
			return ReturnCode::GitMissing;
		if (RUN_COMMAND("gh > " EMPTY_FILE " 2>&1"))
			return ReturnCode::GitHubCLIMissing;

		std::filesystem::path directory = DEFAULT_PROJECT_DIRECTORY;
		bool isPublic = false;
		bool useOLCTemplate = false;

		for (int argi = 2; argi < argc; )
		{
			std::string_view arg = argv[argi++];
			if (arg == "--dir")
			{
				if (argi + 1 < argc)
					directory = argv[argi++];
				else
					return ReturnCode::DirectoryArgMissing;
			}
			else if (arg == "--public")
				isPublic = true;
			else if (arg == "--olc")
				useOLCTemplate = true;
			else
				return ReturnCode::ShowHelpMessage;
		}

		std::filesystem::path projectDirectory = directory / argv[1];

		std::error_code error;
		if (std::filesystem::exists(projectDirectory, error) || error)
			return ReturnCode::ProjectDirectoryAlreadyExists;

		if (!std::filesystem::create_directories(projectDirectory, error) || error)
			return ReturnCode::CouldntCreateDirectory;

		// Create the repository.
		std::string command = "gh repo create \"";
		command.append(argv[1]).append("\" --");
		command.append(isPublic ? "public" : "private");
		command.append(" --template " GITHUB_ACCOUNT_NAME "/");
		command.append(useOLCTemplate ? "OLCTemplate" : "ProjectTemplate");
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode::CouldntCreateRepository;

		// Github sometimes clones the repository as empty.
		// Try to decrease the probability of that I guess?
		// TODO: see if it's possible to get a callback for this or at least poll its status?
		std::this_thread::sleep_for(std::chrono::seconds(2));

		// Clone the repository.
		command = "gh repo clone \"";
		command.append(argv[1]).append(1, '"');
		if (!directory.empty())
			command.append(" \"").append(projectDirectory.string()).append(1, '"');
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode::CouldntCloneRepository;

		std::filesystem::path projectProjectDirectory = projectDirectory / argv[1];

		// Rename the vs solution's project folder to <dir>/<ProjectName>/<ProjectName>.
		std::filesystem::rename(projectDirectory / prjName, projectProjectDirectory, error);
		if (error) return ReturnCode::CouldntRenameFile;

		ReturnCode rc = ReturnCode::Success;

		// Rename the build script <dir>/<ProjectName>/Build<ProjectName>.lua.
		std::filesystem::path projectProjectBuildScript = projectProjectDirectory / std::string("Build").append(argv[1]).append(".lua");
		std::filesystem::rename(projectProjectDirectory / std::string("Build").append(prjName).append(".lua"), projectProjectBuildScript, error);
		if (error) return ReturnCode::CouldntRenameFile;

		// Replace the first "__PROJECT_NAME__" in <dir>/<ProjectName>/<ProjectName>/Build<ProjectName>.lua with <ProjectName>.
		rc = EditFile(projectProjectBuildScript,
		[argv](std::string& file)
		{
			file.replace(file.find(prjName), prjName.size(), argv[1]);
		});
		if (rc != ReturnCode::Success) return rc;

		rc = EditFile(projectDirectory / "BuildAll.lua",
		[argv](std::string& file)
		{
			// Replace "__WORKSPACE_NAME__" with <ProjectName>.
			file.replace(file.find(wksName), wksName.size(), argv[1]);
			// Replace all "__PROJECT_NAME__"'s in <dir>/<ProjectName>/BuildAll.lua with <ProjectName>.
			ReplaceAll(file, prjName, argv[1]);
		});
		if (rc != ReturnCode::Success) return rc;

		rc = EditFile(projectDirectory / "BuildDependencies.lua",
		[argv](std::string& file)
		{
			// Replace the only "__PROJECT_NAME__" in <dir>/<ProjectName>/BuildDependencies.lua with <ProjectName>.
			file.replace(file.find(prjName), prjName.size(), argv[1]);
		});
		if (rc != ReturnCode::Success) return rc;

		if (useOLCTemplate)
		{
			std::filesystem::path projectProjectSrcDirectory = projectProjectDirectory / "src";
			std::filesystem::path projectProjectSrcOLCTemplateH = projectProjectSrcDirectory / "OLCTemplate.h";
			std::filesystem::path projectProjectSrcOLCTemplateCPP = projectProjectSrcDirectory / "OLCTemplate.cpp";
			rc = ReplaceOLCTemplateWithProjectNameInFiles(
			{
				projectDirectory / "README.md",
				projectProjectSrcOLCTemplateH,
				projectProjectSrcOLCTemplateCPP,
				projectProjectSrcDirectory / "main.cpp"

			}, argv[1]);
			if (rc != ReturnCode::Success) return rc;

			std::filesystem::path destName = projectProjectSrcDirectory / argv[1];
			std::filesystem::rename(projectProjectSrcOLCTemplateH, destName.replace_extension("h"), error);
			if (error) return ReturnCode::CouldntRenameFile;
			std::filesystem::rename(projectProjectSrcOLCTemplateCPP, destName.replace_extension("cpp"), error);
			if (error) return ReturnCode::CouldntRenameFile;
		}
		else
		{
			rc = EditFile(projectDirectory / "README.md",
			[argv](std::string& file)
			{
				file.replace(file.find(pjtName), pjtName.size(), argv[1]);
			});
			if (rc != ReturnCode::Success) return rc;
		}

		std::filesystem::current_path(projectDirectory, error);

		// Commit the generated changes to the project's repository.
		command = "git add -A && git commit -m \"Project Generation Commit.\" && git push";
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode::CouldntCommitToRepository;

		std::filesystem::current_path(projectDirectory / "Scripts", error);
		// Call GenerateProjects.bat
		command = "GenerateProjects.bat";
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode::CouldntGenerateProjects;

		// Open visual studio solution as a new process.
		command = START_PROCESS_BEGIN "\"\" \"";
		command.append(projectDirectory.string());
		command.append(1, std::filesystem::path::preferred_separator);
		command.append(argv[1]);
		command.append(".sln\"" START_PROCESS_END);
		if (RUN_COMMAND(command.c_str()))
			return ReturnCode::CouldntOpenVSSolution;
	}
	else
		return ReturnCode::ShowHelpMessage;

	return ReturnCode::Success;
}

ReturnCode EditFile(const std::filesystem::path& filepath, const std::function<void(std::string&)>& func)
{
	std::ifstream inFile(filepath);
	if (!inFile.is_open())
		return ReturnCode::CouldntReadFile;

	std::stringstream inFileStream;
	inFileStream << inFile.rdbuf();
	inFile.close();

	std::string file = inFileStream.str();
	func(file);

	// Write as binary data to not change line endings.
	std::ofstream outFile(filepath, std::ofstream::binary);
	if (!outFile.is_open())
		return ReturnCode::CouldntWriteFile;

	outFile << file;
	outFile.close();

	return ReturnCode::Success;
}

void ReplaceAll(std::string& file, std::string_view replacee, std::string_view replacer)
{
	size_t offset = file.find(replacee);
	while (offset != std::string::npos)
	{
		file.replace(offset, replacee.size(), replacer.data());
		offset += replacer.size();
		offset = file.find(replacee, offset);
	}
}

ReturnCode ReplaceOLCTemplateWithProjectNameInFiles(const std::vector<std::filesystem::path>& filepaths, std::string_view arg1)
{
	ReturnCode rc = ReturnCode::Success;
	for (size_t i = 0; i < filepaths.size() && rc == ReturnCode::Success; ++i)
	{
		rc = EditFile(filepaths[i],
		[arg1](std::string& file)
		{
			ReplaceAll(file, olcName, arg1);
		});
	}
	return rc;
}
