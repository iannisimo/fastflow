#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>
#include <sys/wait.h>


#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>


static inline unsigned long getusec() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (unsigned long)(tv.tv_sec*1e6+tv.tv_usec);
}

std::string configFile;
std::string executable;

inline std::vector<std::string> split (const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;

    while (getline (ss, item, delim))
        result.push_back (item);

    return result;
}

struct G {
    std::string name, host;
    FILE* fd = nullptr;

    template <class Archive>
    void load( Archive & ar ){
        ar(cereal::make_nvp("name", name));
        
        try {
            std::string endpoint;
            ar(cereal::make_nvp("endpoint", endpoint)); std::vector endp(split(endpoint, ':'));
            host = endp[0]; //port = std::stoi(endp[1]);
        } catch (cereal::Exception&) {
            host = "127.0.0.1"; // set the host to localhost if not found in config file!
            ar.setNextName(nullptr);
            }
    }

    void run(){
        char b[150]; // ssh -t
        sprintf(b, " %s %s %s --DFF_Config=%s --DFF_GName=%s", (isRemote() ? "ssh -t " : ""), (isRemote() ? host.c_str() : "") , executable.c_str(), configFile.c_str(), this->name.c_str());
       std::cout << "Executing the following string: " << b << std::endl;
        fd = popen(b, "r");

        if (fd == NULL) {
            printf("Failed to run command\n" );
            exit(1);
        }
    }

    bool isRemote(){return !(!host.compare("127.0.0.1") || !host.compare("localhost"));}


};

bool allTerminated(std::vector<G>& groups){
    for (G& g: groups)
        if (g.fd != nullptr)
            return false;
    return true;
}

int main(int argc, char** argv) {

    if (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-help") == 0 || strcmp(argv[0], "-h") == 0){
        std::cout << "USAGE: " <<  argv[0] << " [options] -f <configFile> <cmd> \n\n"
                  << "OPTIONS: \n"
                  << "\t -v <g1>,...,<g2> \t Print the ouput of the g1 and g2 processes. If no groups are specified all are printed\n";

        exit(EXIT_SUCCESS);
    }


    std::vector<std::string> viewGroups;
    bool seeAll = false;
    
    int c;
    while ((c = getopt(argc, argv, "Vv:f:")) != -1){

        switch (c){
            case 'f':
                configFile = std::string(optarg);
                break;
            case 'V':
                seeAll = true;
                break;
            case 'v':    
                viewGroups = split(optarg, ',');
                break;
            case '?':
                if (optopt == 'f')
                    printf ("Option -%c requires an argument.\n", optopt);
                else if (isprint (optopt))
                    printf ("Unknown option `-%c'.\n", optopt);
                else
                    printf ("Unknown option character `\\x%x'.\n", optopt);
                return 1;
            default:
                abort();
        }
    }
    
    for (int index = optind; index < argc; index++)
        executable += std::string(argv[index]) + " ";
    
    std::ifstream is(configFile);

    if (!is){
        std::cerr << "Unable to open configuration file for the program!" << std::endl;
        return -1;
    }

    std::vector<G> parsedGroups;

    try {
        cereal::JSONInputArchive ar(is);
        ar(cereal::make_nvp("groups", parsedGroups));
    } catch (const cereal::Exception& e){
        std::cerr << "Error parsing the JSON config file. Check syntax and structure of  the file and retry!" << std::endl;
        exit(EXIT_FAILURE);
    }

    #ifdef DEBUG
        for(auto& g : parsedGroups)
            std::cout << "Group: " << g.name << " on host " << g.host << std::endl;
    #endif

    auto Tstart = getusec();

    for (G& g : parsedGroups)
        g.run();
    
    while(!allTerminated(parsedGroups)){
        for(G& g : parsedGroups){
            if (g.fd != nullptr){
                char buff[1024];
                char* result = fgets(buff, sizeof(buff), g.fd);
                if (result == NULL){
                    int code = pclose(g.fd);
                    if (WEXITSTATUS(code) != 0)
                        std::cout << "[" << g.name << "][ERR] Report an return code: " << WEXITSTATUS(code) << std::endl;
                    g.fd = nullptr;
                } else {
                    if (seeAll || find(viewGroups.begin(), viewGroups.end(), g.name) != viewGroups.end())
                        std::cout << "[" << g.name << "]" << buff;
                }
            }
        }

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    
    std::cout << "Elapsed time: " << (getusec()-(Tstart))/1000 << " ms" << std::endl;


    return 0;
}