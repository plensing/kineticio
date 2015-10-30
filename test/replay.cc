#include <kio/KineticIoFactory.hh>
#include <kio/FileIoInterface.hh>
#include <string>
#include <string.h>
#include <fstream>
#include <sstream>
#include <vector>

using std::string;

struct Configuration{
  string kineticfile; 
  string patternfile; 
  bool read;
  bool write;
};

bool mshouldLog(const char* func, int level){
    return true;
}

void mlog(const char* func, const char* file, int line, int level, const char* msg){
  fprintf(stdout, " %s\n", msg);
}


void parseArguments(int argc, char** argv, Configuration& config) {
  
  for(int i = 1; i < argc; i++){
    if(strcmp("-path", argv[i]) == 0)
      config.kineticfile = argv[i+1];
    else if (strcmp("-pattern", argv[i]) == 0)
      config.patternfile = argv[i+1];      
    else if(strcmp("read",argv[i]) == 0)
      config.read = true;
    else if(strcmp("write",argv[i]) == 0)
      config.write = true;
  }
  
  printf("Configuration:\n");
  printf("Kinetic Path (has to be in form kinetic:cluster:filename): %s\n",config.kineticfile.c_str());
  printf("Pattern File (comma seperated offset+length values): %s\n",config.patternfile.c_str());
  printf("Read: %d, Write: %d\n",config.read,config.write);
  
}


int main(int argc, char** argv)
{
  Configuration config{"invalid","invalid",false,false}; 
  parseArguments(argc, argv, config);
    
  
  kio::Factory::registerLogFunction(mlog, mshouldLog);
  auto fio = kio::Factory::makeFileIo();
  fio->Open(config.kineticfile, config.write ? SFS_O_CREAT : 0);
  
  struct stat s; 
  fio->Stat(&s);
  printf("File is %ld bytes long.\n",s.st_size);
  
  std::ifstream fs(config.patternfile);
  std::string line; 
  std::vector<char> buffer;
  
  
  while (std::getline(fs, line))
  {
    std::stringstream current;
    current << line;

    std::string token;
    std::getline(current, token, ',');
    long offset = std::atoi(token.c_str());
    std::getline(current, token);
    long length = std::atoi(token.c_str());
    
    if(length > buffer.size())
      buffer.resize(length);

    if(config.read){
      int bytes = fio->Read(offset, buffer.data(), length);
      printf("read %d bytes. Requested (offset,length) : (%ld,%ld)\n", bytes, offset, length);
    }
    if(config.write){
      int bytes = fio->Write(offset, buffer.data(), length);
      printf("written %d bytes. Requested (offset,length) : (%ld,%ld)\n", bytes, offset, length);
    }
  }  
  fio->Close();
  
  printf("done\n");
  
}