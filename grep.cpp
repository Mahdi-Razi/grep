/* compile the file with command line g++ -std=c++17*/
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <vector>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <regex>

std::unordered_set<std::string> visited; // unorderes set visited for keeping track of searched paths
std::queue<std::pair<std::filesystem::path, int>> directories; // shared memory queue for directories
std::mutex mtx; // mutex lock for handling critical sections
std::condition_variable cv; // condition variable for calling a waiting thread


bool search_in_file(const std::filesystem::path& path, const std::string& pattern, bool invert_match, bool line_n, bool files_without_match) {
    // opens file in given path
    std::ifstream file(path);
    std::string line;
    // define a case insensitive regular expression
    // for given pattern
    std::regex rgx(pattern, std::regex_constants::icase);
    // for printing line numbers if -n is enabled
    int line_number = 0;
    // return value. true if match is found
    // false otherwise
    bool flag = false;

    // while there is a line to read from file
    while (std::getline(file, line)) {
        ++line_number;
        // checks the line for a match
        bool matches = std::regex_search(line, rgx);

        if(!flag && matches) {
            flag = true;
            // if -L is enabled
            if(files_without_match) {
                return true;
            }
        }
        // print the line either there is a match and -v is disabled
        // or there is no match and -v is enabled
        if ((matches && !invert_match) || (!matches && invert_match)) {
            // if -n is enabled
            if(line_n)
                std::cout << "Line " << line_number << ": ";
            
            std::cout << line << std::endl;
        }
    }

    return flag;
}

void search_dir(bool search_for_files, const std::vector<std::string>& file_names, const std::string& pattern, bool invert_match, bool line_number, bool files_without_match, int max_depth)
{
    // define a case insensitive regular expression
    // for given pattern
    std::regex rgx(pattern, std::regex_constants::icase);

    while(true) {

        std::pair<std::filesystem::path, int> path_depth;

        {
            // before accessing the critical section
            // wait for the mutex lock
            std::unique_lock<std::mutex> lock(mtx);
            
            // If there are no more directories stop the search
            if (directories.empty()) {
                return; 
            }

            // otherwise grab the front directory
            // and then remove it from shared memory
            path_depth = directories.front();
            directories.pop();

            // thread signals the mutex lock
        }

        std::filesystem::path path = path_depth.first;
        int depth = path_depth.second;
        
        // if depth of new path is greater than
        // max depth stop the thread search
        if (depth >= max_depth)
            return;

        try {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {

                if (entry.is_directory()) {
                    // Get the absolute path
                    auto abs_path = std::filesystem::canonical(entry.path());
                    // Try to insert the path into the visited set
                    auto [it, inserted] = visited.insert(abs_path.string());

                    if (inserted) {
                        std::unique_lock<std::mutex> lock(mtx); // wait for mutex lock
                        directories.push({abs_path, depth + 1}); // add the unvisited path to directories
                        cv.notify_one(); // notify one waiting thread
                        // signal the mutex lock
                    }
                    
                    // if checking the files and folders names
                    // for a match with pattern
                    if(search_for_files) {
                        std::string str = entry.path().filename();
                        // check weather the pattern matches
                        // the folder's name
                        if (std::regex_search(str, rgx))
                            std::cout << "\033[1;31m" << str << "\033[0m: " <<  entry.path() << std::endl;
                    }

                } else {

                    if(search_for_files) {
                        std::string str = entry.path().filename();
                        if (std::regex_search(str, rgx))
                            std::cout << "\033[1;31m" << str << "\033[0m: " <<  entry.path() << std::endl;
                    } else {
                        for (size_t i = 0; i < file_names.size(); ++i) {
                            // if a file is found
                            if (entry.path().filename() == file_names[i]) { 
                                
                                if(!files_without_match)
                                    std::cout << "\033[1;31m" << entry.path().filename() << "\033[0m: " <<  entry.path() << std::endl;
                                
                                // f is true if there is at least one
                                // match in the file and false otherwise
                                bool f = search_in_file(entry.path(), pattern, invert_match, line_number, files_without_match);
                                
                                // if -L is enabled and there is no match
                                // print the file's name
                                if(files_without_match && !f)
                                    std::cout<< "\033[1;31m" << file_names[i] << "\033[0m"<< std::endl;
                                
                                break;
                            }
                        }
                    }
                }
            }
        } catch (std::filesystem::filesystem_error& e) {
            // std::cerr << e.what() << std::endl;
        }
    }
}


int main(int argc, char* argv[]) {
    // root directory for
    // starting search
    std::string root = "/";
    // default max depth
    int max_depth = 4;
    // number of threads initially
    // is set to the number of cpus
    int num_threads = std::thread::hardware_concurrency();

    std::string pattern;
    std::vector<std::string> file_names;
    bool invert_match = false; // -v
    bool line_number = false; // -n
    bool files_without_match = false; // -L
    bool search_for_files = false; // -f

    /*
    Command line format is [OPTIONS] "PATTERN" [FILES]
    -v : Invert the sense of matching, to select non-matching lines
    -n : Prefix each line of output with the line number within its input file
    -L : Prints the file name that does not contain the provided matching pattern.
    -f : Search for the provided pattern in files and folders names.
    -d= : Sets the maximum search depth to the integer after = (for example -d=3)
    -th= : Sets the number of threads to the integer after = (for example -th=6)

    note: if -f is enabled, there is no need to enter [FILES]
    */

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v") {
            invert_match = true;
        } else if (arg == "-n") {
            line_number = true;
        } else if(arg == "-L") {
            files_without_match = true;
        } else if(arg == "-f") {
            search_for_files = true;
        } else if (arg.substr(0, 3) == "-d=") {
            max_depth = stoi(arg.substr(3));
        } else if (arg.substr(0, 4) == "-th=") {
            num_threads = stoi(arg.substr(4));
        } else {
            if (pattern.empty()) {
                pattern = argv[i];
            } else {
                file_names.push_back(argv[i]);
            }
        }
    }

    // handling exceptions
    // for example -v and -f cant be used together
    if(search_for_files) {
        if(invert_match || line_number || files_without_match) {
            std::cerr << "undefined command line" << std::endl;
            return 1;
        }
    }

    if(files_without_match) {
        if(invert_match || line_number || search_for_files) {
            std::cerr << "undefined command line" << std::endl;
            return 1;
        }
    }

    // push the first directory root with depth 0
    // into shared memory directories
    directories.push({root, 0});
    visited.insert(root);

    // create a vector of threads with size num_threads
    std::vector<std::thread> threads(num_threads);

    // start each thread for searching
    for (auto& t : threads) {
        t = std::thread(search_dir, search_for_files, file_names, pattern, invert_match, line_number, files_without_match, max_depth);
    }

    // wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }

    return 0;
}