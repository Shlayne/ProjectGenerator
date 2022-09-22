#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

#if SYSTEM_WINDOWS
	#define EMPTY_FILE "nul"
#else
	#define EMPTY_FILE "/dev/null"
#endif

int main(int argc, char** argv)
{
	/* TODO: 
		1) Finish adding error messages for when things go wrong.
		2) Streamline the system calls.
		3) Make something like this possible:
			"ProjectGenerator.exe Calculator C:\Workspace\Programming\Dev\C++ --olc --public"
			where "--public" can be "--private" or ommitted,
			and "--olc" can be ommitted which changes the repo it uses as a template.
		4) Figure out how to deal with (aka remove) the pause at the end of calling "GenerateProjects.bat".
		5) Potentially add a file to cache the directory so I don't have to type it each time.
	*/

	bool showHelpMessage = false;
	if (argc == 1 || argc == 2 && std::string_view("--help") == argv[1])
		showHelpMessage = true;
	else if (argc > 1)
	{
		std::filesystem::path projectDirectory = argv[1];
		if (argc > 2)
			projectDirectory = argv[2] / projectDirectory;

		std::error_code error;
		if (std::filesystem::create_directory(projectDirectory, error))
		{
			if (!system("gh > " EMPTY_FILE " 2>&1"))
			{
				std::string command = "gh repo create ";
				command.append(1, '"').append(argv[1]).append(1, '"').append(" --public --template Shlayne/ProjectTemplate");

				if (!system(command.c_str()))
				{
					command = "gh repo clone \"";
					command.append(argv[1]).append(1, '"');
					if (argc > 2)
						command.append(" \"").append(argv[2]).append(1, '"');

					if (!system(command.c_str()))
					{
						constexpr char prjName[] = "__PROJECT_NAME__";
						constexpr auto prjNameLength = sizeof(prjName) / sizeof(*prjName) - 1;
						constexpr char wksName[] = "__WORKSPACE_NAME__";
						constexpr auto wksNameLength = sizeof(wksName) / sizeof(*wksName) - 1;

						// Rename "<dir>/<ProjectName>/__PROJECT_NAME__" to "<dir>/<ProjectName>/<ProjectName>".
						std::filesystem::path oldPath = projectDirectory;
						oldPath /= prjName;
						std::filesystem::path newPath = projectDirectory;
						newPath /= argv[1];
						std::filesystem::rename(oldPath, newPath, error);

						// Replace the first "__PROJECT_NAME__" in "<dir>/<ProjectName>/<ProjectName>/premake5.lua" with "<ProjectName>".
						{
							std::filesystem::path filepath = newPath / "premake5.lua";
							std::ifstream inFile(filepath);
							if (inFile.is_open())
							{
								std::stringstream inFileStream;
								inFileStream << inFile.rdbuf();
								inFile.close();

								std::string file = inFileStream.str();
								file.replace(file.find(prjName), prjNameLength, argv[1]);

								std::ofstream outFile(filepath);
								if (outFile.is_open())
								{
									outFile.write(file.c_str(), file.size());
									outFile.close();
								}
								else
								{
									// todo
								}
							}
							else
							{
								// todo
							}
						}

						// Replace the first and last "__PROJECT_NAME__" in "<dir>/<ProjectName>/premake5.lua" with "<ProjectName>"
						// and "__WORKSPACE_NAME__" with "<ProjectName>".
						{
							std::filesystem::path filepath = projectDirectory / "premake5.lua";
							std::ifstream inFile(filepath);
							if (inFile.is_open())
							{
								std::stringstream inFileStream;
								inFileStream << inFile.rdbuf();
								inFile.close();

								std::string file = inFileStream.str();
								file.replace(file.find(wksName), wksNameLength, argv[1]);
								file.replace(file.find(prjName), prjNameLength, argv[1]);
								file.replace(file.rfind(prjName), prjNameLength, argv[1]);

								std::ofstream outFile(filepath);
								if (outFile.is_open())
								{
									outFile.write(file.c_str(), file.size());
									outFile.close();
								}
								else
								{
									// todo
								}
							}
							else
							{
								// todo
							}
						}

						// Call "GenerateProjects.bat".
						command = "pushd \"";
						command.append(projectDirectory.string())
							.append(1, std::filesystem::path::preferred_separator)
							.append("Scripts\" && call \".")
							.append(1, std::filesystem::path::preferred_separator)
							.append("GenerateProjects.bat\" && popd");
						if (!system(command.c_str()))
						{
							command = argv[1];
							command.append(1, std::filesystem::path::preferred_separator)
								.append(argv[1])
								.append(".sln");
							if (!system(command.c_str()))
							{

							}
							else
								std::cerr << "Failed to open visual studio solution.\n";
						}
						else
							std::cerr << "Failed to generate projects.\n";
					}
					else
						std::cerr << "Failed to clone repository.\n";
				}
				else
					std::cerr << "Failed to create repository.\n";
			}
			else
				std::cerr << "Must have GitHub CLI installed. Get it here: \"https://cli.github.com/\".\n";
		}
	}
	else
		showHelpMessage = true;

	if (showHelpMessage)
		std::cout << "Usage: " << argv[0] << " <ProjectName> [dir]\n";

	return 0;
}
