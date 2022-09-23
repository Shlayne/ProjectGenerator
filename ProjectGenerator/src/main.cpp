#include <iostream>
#include <fstream>
#include <filesystem>
#include <functional>
#include <sstream>

#if SYSTEM_WINDOWS
	#define EMPTY_FILE "nul"
	#define START_PROCESS_BEGIN "start "
	#define START_PROCESS_END
#else
	#define EMPTY_FILE "/dev/null"
	#define START_PROCESS_BEGIN
	#define START_PROCESS_END " &"
#endif

enum class ReturnCode : int
{
	Success = 0,
	ShowHelpMessage,
	GitHubCLIMissing,
	FileAlreadyExists,
	CouldntCreateDirectory,
	CouldntCreateRepository,
	CouldntCloneRepository,
	CouldntReadFile,
	CouldntWriteFile,
	CouldntGenerateProjects,
	CouldntOpenVSSolution,
};

ReturnCode Run(int argc, const char* const* argv);

int main(int argc, char** argv)
{
	ReturnCode rc = Run(argc, argv);
	int irc = static_cast<int>(rc);
	if (rc != ReturnCode::Success)
	{
		if (rc == ReturnCode::ShowHelpMessage)
		{
			rc = ReturnCode::Success;
			std::cout << "Usage: " << argv[0] << " [<ProjectName> [options] | --help]\n\n";
			std::cout << "Options:\n";
			std::cout << "   --dir filepath   Set the local directory of the project\n";
			std::cout << "   --private        Make the project's repository private\n";
			std::cout << "   --olc            Use OLCTemplate instead of ProjectTemplate\n";
			std::cout << '\n';
		}
		else
		{
			constexpr const char* errorMessages[]
			{
				"Must have GitHub CLI installed. Get it here: https://cli.github.com/",
				"A file already exists at the project directory.",
				"Couldn't create project directory.",
				"Couldn't create repository.",
				"Couldn't clone repository.",
				"Couldn't read file.",
				"Couldn't write file.",
				"Couldn't generate projects.",
				"Couldn't open Visual Studio solution.",
			};
			std::cerr << errorMessages[irc - 2] << '\n';
		}
	}
	return irc;
}

static constexpr char olcName[] = "OLCTemplate";
static constexpr auto olcNameLength = sizeof(olcName) / sizeof(*olcName) - 1;
static constexpr char prjName[] = "__PROJECT_NAME__";
static constexpr auto prjNameLength = sizeof(prjName) / sizeof(*prjName) - 1;
static constexpr char wksName[] = "__WORKSPACE_NAME__";
static constexpr auto wksNameLength = sizeof(wksName) / sizeof(*wksName) - 1;

ReturnCode EditFile(const std::filesystem::path& filepath, const std::function<void(std::string&)>& func);
void ReplaceOLCTemplateWithProjectName(std::string& file, const char* arg1);
ReturnCode ReplaceOLCTemplateWithProjectNameInFiles(const std::vector<std::filesystem::path>& filepaths, const char* arg1);

ReturnCode Run(int argc, const char* const* argv)
{
	if (argc == 1 || (argc > 1 && std::string_view("--help") == argv[1]))
		return ReturnCode::ShowHelpMessage;
	else if (argc > 1)
	{
		if (system("gh > " EMPTY_FILE " 2>&1"))
			return ReturnCode::GitHubCLIMissing;

		std::filesystem::path directory;
		bool isPrivate = false;
		bool useOLCTemplate = false;

		for (int argi = 2; argi < argc; )
		{
			std::string_view arg = argv[argi++];
			if (arg == "--dir")
				directory = argv[argi++];
			else if (arg == "--private")
				isPrivate = true;
			else if (arg == "--olc")
				useOLCTemplate = true;
			else
				return ReturnCode::ShowHelpMessage;
		}

		std::filesystem::path projectDirectory = directory / argv[1];

		std::error_code error;
		if (std::filesystem::exists(projectDirectory, error) || error)
			return ReturnCode::FileAlreadyExists;

		if (!std::filesystem::create_directory(projectDirectory, error) || error)
			return ReturnCode::CouldntCreateDirectory;

		// Create the repository.
		std::string command = "gh repo create \"";
		command.append(argv[1]).append("\" --");
		command.append(isPrivate ? "private" : "public");
		command.append(" --template Shlayne/");
		command.append(useOLCTemplate ? "OLCTemplate" : "ProjectTemplate");
		if (system(command.c_str()))
			return ReturnCode::CouldntCreateRepository;

		// Clone the repository.
		command = "gh repo clone \"";
		command.append(argv[1]).append(1, '"');
		if (!directory.empty())
			command.append(" \"").append(projectDirectory.string()).append(1, '"');
		if (system(command.c_str()))
			return ReturnCode::CouldntCloneRepository;

		std::filesystem::path projectProjectDirectory = projectDirectory;
		projectProjectDirectory /= argv[1];

		// Rename the vs solution's project folder to "<dir>/<ProjectName>/<ProjectName>".
		std::filesystem::rename(projectDirectory / (useOLCTemplate ? olcName : prjName), projectProjectDirectory, error);

		// Replace the first "__PROJECT_NAME__" in "<dir>/<ProjectName>/<ProjectName>/premake5.lua" with "<ProjectName>".
		ReturnCode rc = EditFile(projectProjectDirectory / "premake5.lua",
		[argv, useOLCTemplate](std::string& file)
		{
			const char* name = prjName;
			size_t nameLength = prjNameLength;
			if (useOLCTemplate)
			{
				name = olcName;
				nameLength = olcNameLength;
			}
			file.replace(file.find(name), nameLength, argv[1]);
		});
		if (rc != ReturnCode::Success)
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
				file.replace(file.find(wksName), wksNameLength, argv[1]);
				file.replace(file.find(prjName), prjNameLength, argv[1]);
				file.replace(file.rfind(prjName), prjNameLength, argv[1]);
			}
		});
		if (rc != ReturnCode::Success)
			return rc;

		if (useOLCTemplate)
		{
			std::filesystem::path projectProjectSrcDirectory = projectProjectDirectory / "src";
			std::filesystem::path projectProjectSrcOLCTemplateH = projectProjectSrcDirectory / "OLCTemplate.h";
			std::filesystem::path projectProjectSrcOLCTemplateCPP = projectProjectSrcDirectory / "OLCTemplate.cpp";
			rc = ReplaceOLCTemplateWithProjectNameInFiles(
			{
				projectDirectory / "Dependencies/Dependencies.lua",
				projectProjectSrcOLCTemplateH,
				projectProjectSrcOLCTemplateCPP,
				projectProjectSrcDirectory / "main.cpp"
			}, argv[1]);
			if (rc != ReturnCode::Success)
				return rc;

			std::filesystem::path destName = projectProjectSrcDirectory / argv[1];
			std::filesystem::rename(projectProjectSrcOLCTemplateH, destName.replace_extension("h"), error);
			std::filesystem::rename(projectProjectSrcOLCTemplateCPP, destName.replace_extension("cpp"), error);
		}

		// Call "GenerateProjects.bat" in the appropriate directory.
		command = "pushd \"";
		command.append(projectDirectory.string());
		command.append(1, std::filesystem::path::preferred_separator);
		command.append("Scripts");
		command.append(1, std::filesystem::path::preferred_separator);
		command.append("\" && GenerateProjects.bat && popd");
		if (system(command.c_str()))
			return ReturnCode::CouldntGenerateProjects;

		// Open visual studio solution.
		command = START_PROCESS_BEGIN "\"\" \"";
		command.append(projectDirectory.string());
		command.append(1, std::filesystem::path::preferred_separator);
		command.append(argv[1]);
		command.append(".sln\"" START_PROCESS_END);
		if (system(command.c_str()))
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

	std::ofstream outFile(filepath);
	if (!outFile.is_open())
		return ReturnCode::CouldntWriteFile;

	outFile.write(file.c_str(), file.size());
	outFile.close();

	return ReturnCode::Success;
}

void ReplaceOLCTemplateWithProjectName(std::string& file, const char* arg1)
{
	size_t projectNameLength = strlen(arg1);
	size_t offset = 0;
	while (true) // This is completely fine, stop complaining.
	{
		offset = file.find(olcName, offset);
		if (offset == std::string::npos)
			break;
		file.replace(offset, olcNameLength, arg1);
		offset += projectNameLength;
	}
}

ReturnCode ReplaceOLCTemplateWithProjectNameInFiles(const std::vector<std::filesystem::path>& filepaths, const char* arg1)
{
	ReturnCode rc = ReturnCode::Success;
	for (size_t i = 0; i < filepaths.size() && rc == ReturnCode::Success; i++)
	{
		rc = EditFile(filepaths[i],
		[arg1](std::string& file)
		{
			ReplaceOLCTemplateWithProjectName(file, arg1);
		});
	}
	return rc;
}
