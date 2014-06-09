#include <iostream>
#include <cmath>
#include <fstream>
#include <bitset>

typedef unsigned int uint32_t;

/* To match kernel module data structure */
const int DataPointsRPi=10000;
struct DataStructRPi{
  uint32_t Buffer[DataPointsRPi];
  uint32_t time;
};

/*------------------------------------------------*/

int main(){
  //Read the RPi
  struct DataStructRPi dataStruct;
  unsigned char *ScopeBufferStart;
  unsigned char *ScopeBufferStop;
  unsigned char *buf_p;

  buf_p=(unsigned char*)&dataStruct;
  ScopeBufferStart=(unsigned char*)&dataStruct;
  ScopeBufferStop=ScopeBufferStart+
    sizeof(struct DataStructRPi);

  std::string line;
  std::ifstream myfile ("/dev/chardev");
  if(myfile.is_open()){
    while(std::getline(myfile,line)){
      for(int i=0;i<line.size();i++){
        if(buf_p>ScopeBufferStop)
          std::cerr<<"buf_p out of range!"<<std::endl;
        *(buf_p)=line[i];
        buf_p++;
      }
    }
    myfile.close();
  }
  else std::cerr<<"Unable to open file"<<std::endl;

  // Now convert data for text output

  // Time in nano seconds
  double time=dataStruct.time/(double)DataPointsRPi;

  for(int i=0;i<DataPointsRPi;i++){
    int valueADC1=0;//ADC 1
    // Move the bits to the right position
    int tmp = dataStruct.Buffer[i] & (0b11111<<(7));
    valueADC1=tmp>>(7);
    tmp = dataStruct.Buffer[i] & (0b1<<(25));
    valueADC1+=(tmp>>(20));

    int valueADC2=0;//ADC2
    tmp = dataStruct.Buffer[i] & (0b11<<(17));
    valueADC2=tmp>>17;
    tmp=dataStruct.Buffer[i] & (0b111<<(22));
    valueADC2+=(tmp>>20);
    tmp=dataStruct.Buffer[i] & (0b1<<27);
    valueADC2+=(tmp>>22);

    // Print the values of the time and both ADCs
    std::cout<<i*time<<"\t"<<valueADC1*(5./63.)
             <<"\t"<<valueADC2*(5./63.)<<std::endl;
  }
  return 0;
}
