#!/bin/bash
# ./scaffold_C-C++_project.sh  will execute the script
# The script will prompt the user for the project name and root directory.
# It will then create the project directory and subdirectories. if properly executed 

# functions for creating directories and files and handling errors
create_directory() {
    mkdir -p "$1" || { echo "Failed to create directory $1"; exit 1; }
}

create_file() {
    touch "$1" || { echo "Failed to create file $1"; exit 1; }
}

prompt_for_project_type() {
        echo "Welcome to the C / C++ scaffolding script!"
    while true; do
        read -p "Enter project type (C/C++): or (press ^C to exit)" project_type
        case "$project_type" in
            C|c) project_type="C"; break;;
            C++|c++) project_type="C++"; break;;
            *) echo "Invalid input. Please enter 'C' or 'C++'.";;
        esac
    done
}

# Function to prompt for project name
prompt_for_project_name() {
    while true; do
        read -p "Enter the root directories absolute path for your project: " project_root
        if [ ! -d "$project_root" ]; then
            echo "The specified root directory does not exist. Please enter a valid directory."
            continue
        fi
        read -p "Enter the name of your C/C++ project (press ^C to exit): " project_name
        if [[ -n "$project_name" ]]; then
            break
        else
            echo "No project name provided. Please enter a project name."
        fi
    done
}

prompt_for_project_type
prompt_for_project_name
# Ensure the project_root is an absolute path
if [[ "$project_root" != /* ]]; then
    project_root="$(pwd)/$project_root"
fi
# Change to the user's desired directory
cd "$project_root" || { echo "Failed to change to directory $project_root"; exit 1; }
# Create the project directory
create_directory "$project_name"
# Define the full path for the project directory
root_dir="$(pwd)/$project_name"

# /Users/guymorganb/Desktop/GitHub_Repos/C_Programmming/

# Function to detect the operating system
detect_os() {
    case "$(uname -s)" in
        Linux*)   os_name=Linux;;
        Darwin*)  os_name=Mac;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*) 
                  os_name=Windows;;
        *)        os_name="UNKNOWN"
    esac
    echo ${os_name}
}

# Function to check if the compiler is installed
check_compiler() {
    if [ -x "$(command -v clang)" ]; then
        echo "Using clang as the compiler."
    elif [ -x "$(command -v gcc)" ]; then
        echo "Using gcc as the compiler."
    else
        echo "Error: neither clang nor gcc is installed." >&2
        os_name=$(detect_os)
        case "${os_name}" in
            Linux)
                echo "On Linux, you can install clang using your distribution's package manager, e.g., 'sudo apt-get install clang' for Debian/Ubuntu."
                ;;
            Mac)
                echo "On MacOS, you can install clang using Homebrew: 'brew install llvm'."
                ;;
            Windows)
                echo "On Windows, you can install a compiler via Chocolatey (e.g., 'choco install llvm') or download it from the official website."
                ;;
            *)
                echo "Unable to determine your operating system. Please manually install clang or gcc."
                ;;
        esac
        exit 1
    fi
}

# get the paths for the vscode config templates
prompt_for_vscode_templates() {
    echo "VS_Code templates: Enter the paths for the VS Code templates specific to your chosen compiler:"
    while true; do
        read -p "VS_Code templates: Enter the path for tasks.json: " tasks_json
        read -p "VS_Code templates: Enter the path for launch.json: " launch_json
        read -p "VS_Code templates: Enter the path for c_cpp_properties.json: " cpp_properties_json
        if [[ ! -f "$tasks_json" || ! -f "$launch_json" || ! -f "$cpp_properties_json" ]]; then
            echo "VS_Code templates: Please enter a valid path for each template."
            continue
        else
            break
        fi
    done    
}

# Function to create VS Code configuration files
create_vscode_config() {
    vscode_dir="$root_dir/.vscode"
    create_directory "$vscode_dir"
    cp "$tasks_json" "$vscode_dir" && cp "$launch_json" "$vscode_dir" && cp "$cpp_properties_json" "$vscode_dir" || { echo "Failed to copy VS Code templates"; exit 1; }
}

# Call the function to check if clang is installed
check_compiler
# Call the function to prompt for VS Code template paths
prompt_for_vscode_templates
# setup config for vscode
create_vscode_config

# Create directories
create_directory "$root_dir/src/module1"
create_directory "$root_dir/src/module2"
create_directory "$root_dir/include/module1"
create_directory "$root_dir/include/module2"
create_directory "$root_dir/lib/third_party_lib"
create_directory "$root_dir/bin"
create_directory "$root_dir/obj"
create_directory "$root_dir/test/module1"
create_directory "$root_dir/test/module2"
create_directory "$root_dir/doc"

# File extension based on project type
src_ext=".c"
header_ext=".h"
if [ "$project_type" = "C++" ]; then
    src_ext=".cpp"
    header_ext=".hpp"
fi

# Create main source file and module files
create_file "$root_dir/src/main$src_ext"
create_file "$root_dir/src/module1/module1$src_ext"
create_file "$root_dir/src/module1/module1$header_ext"
create_file "$root_dir/src/module2/module2$src_ext"
create_file "$root_dir/src/module2/module2$header_ext"

# Create public header files
create_file "$root_dir/include/module1/module1$header_ext"
create_file "$root_dir/include/module2/module2$header_ext"

# Create test files
create_file "$root_dir/test/test_main$src_ext"
create_file "$root_dir/test/module1/test_module1$src_ext"
create_file "$root_dir/test/module2/test_module2$src_ext"

# Create Makefile and README
create_file "$root_dir"/Makefile
create_file "$root_dir"/README.md

echo "$project_type" project scaffold created at "$root_dir"

# set -e at the start of your script will cause the script to exit immediately if any command exits with a non-zero status (which usually indicates an error).
# set -e 
# set -x will cause the script to print out each command before executing it.
# chmod +x scaffold_C-C++_project.sh  will make the script executable
EOFcat > scripts/setup_project.sh << 'EOF'
#!/bin/bash
# ./scaffold_C-C++_project.sh  will execute the script
# The script will prompt the user for the project name and root directory.
# It will then create the project directory and subdirectories. if properly executed 

# functions for creating directories and files and handling errors
create_directory() {
    mkdir -p "$1" || { echo "Failed to create directory $1"; exit 1; }
}

create_file() {
    touch "$1" || { echo "Failed to create file $1"; exit 1; }
}

prompt_for_project_type() {
        echo "Welcome to the C / C++ scaffolding script!"
    while true; do
        read -p "Enter project type (C/C++): or (press ^C to exit)" project_type
        case "$project_type" in
            C|c) project_type="C"; break;
            C++|c++) project_type="C++"; break;
            *) echo "Invalid input. Please enter 'C' or 'C++'.";
        esac
    done
}

# Function to prompt for project name
prompt_for_project_name() {
    while true; do
        read -p "Enter the root directories absolute path for your project: " project_root
        if [ ! -d "$project_root" ]; then
            echo "The specified root directory does not exist. Please enter a valid directory."
            continue
        fi
        read -p "Enter the name of your C/C++ project (press ^C to exit): " project_name
        if [[ -n "$project_name" ]]; then
            break
        else
            echo "No project name provided. Please enter a project name."
        fi
    done
}

prompt_for_project_type
prompt_for_project_name
# Ensure the project_root is an absolute path
if [[ "$project_root" != /* ]]; then
    project_root="$(pwd)/$project_root"
fi
# Change to the user's desired directory
cd "$project_root" || { echo "Failed to change to directory $project_root"; exit 1; }
# Create the project directory
create_directory "$project_name"
# Define the full path for the project directory
root_dir="$(pwd)/$project_name"

# /Users/guymorganb/Desktop/GitHub_Repos/C_Programmming/

# Function to detect the operating system
detect_os() {
    case "$(uname -s)" in
        Linux*)   os_name=Linux;;
        Darwin*)  os_name=Mac;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*) 
                  os_name=Windows;;
        *)        os_name="UNKNOWN"
    esac
    echo ${os_name}
}

# Function to check if the compiler is installed
check_compiler() {
    if [ -x "$(command -v clang)" ]; then
        echo "Using clang as the compiler."
    elif [ -x "$(command -v gcc)" ]; then
        echo "Using gcc as the compiler."
    else
        echo "Error: neither clang nor gcc is installed." >&2
        os_name=$(detect_os)
        case "${os_name}" in
            Linux)
                echo "On Linux, you can install clang using your distribution's package manager, e.g., 'sudo apt-get install clang' for Debian/Ubuntu."
                ;;
            Mac)
                echo "On MacOS, you can install clang using Homebrew: 'brew install llvm'."
                ;;
            Windows)
                echo "On Windows, you can install a compiler via Chocolatey (e.g., 'choco install llvm') or download it from the official website."
                ;;
            *)
                echo "Unable to determine your operating system. Please manually install clang or gcc."
                ;;
        esac
        exit 1
    fi
}

# get the paths for the vscode config templates
prompt_for_vscode_templates() {
    echo "VS_Code templates: Enter the paths for the VS Code templates specific to your chosen compiler:"
    while true; do
        read -p "VS_Code templates: Enter the path for tasks.json: " tasks_json
        read -p "VS_Code templates: Enter the path for launch.json: " launch_json
        read -p "VS_Code templates: Enter the path for c_cpp_properties.json: " cpp_properties_json
        if [[ ! -f "$tasks_json" || ! -f "$launch_json" || ! -f "$cpp_properties_json" ]]; then
            echo "VS_Code templates: Please enter a valid path for each template."
            continue
        else
            break
        fi
    done    
}

# Function to create VS Code configuration files
create_vscode_config() {
    vscode_dir="$root_dir/.vscode"
    create_directory "$vscode_dir"
    cp "$tasks_json" "$vscode_dir" && cp "$launch_json" "$vscode_dir" && cp "$cpp_properties_json" "$vscode_dir" || { echo "Failed to copy VS Code templates"; exit 1; }
}

# Call the function to check if clang is installed
check_compiler
# Call the function to prompt for VS Code template paths
prompt_for_vscode_templates
# setup config for vscode
create_vscode_config

# Create directories
create_directory "$root_dir/src/module1"
create_directory "$root_dir/src/module2"
create_directory "$root_dir/include/module1"
create_directory "$root_dir/include/module2"
create_directory "$root_dir/lib/third_party_lib"
create_directory "$root_dir/bin"
create_directory "$root_dir/obj"
create_directory "$root_dir/test/module1"
create_directory "$root_dir/test/module2"
create_directory "$root_dir/doc"

# File extension based on project type
src_ext=".c"
header_ext=".h"
if [ "$project_type" = "C++" ]; then
    src_ext=".cpp"
    header_ext=".hpp"
fi

# Create main source file and module files
create_file "$root_dir/src/main$src_ext"
create_file "$root_dir/src/module1/module1$src_ext"
create_file "$root_dir/src/module1/module1$header_ext"
create_file "$root_dir/src/module2/module2$src_ext"
create_file "$root_dir/src/module2/module2$header_ext"

# Create public header files
create_file "$root_dir/include/module1/module1$header_ext"
create_file "$root_dir/include/module2/module2$header_ext"

# Create test files
create_file "$root_dir/test/test_main$src_ext"
create_file "$root_dir/test/module1/test_module1$src_ext"
create_file "$root_dir/test/module2/test_module2$src_ext"

# Create Makefile and README
create_file "$root_dir"/Makefile
create_file "$root_dir"/README.md

echo "$project_type" project scaffold created at "$root_dir"

# set -e at the start of your script will cause the script to exit immediately if any command exits with a non-zero status (which usually indicates an error).
# set -e 
# set -x will cause the script to print out each command before executing it.
# chmod +x scaffold_C-C++_project.sh  will make the script executable
