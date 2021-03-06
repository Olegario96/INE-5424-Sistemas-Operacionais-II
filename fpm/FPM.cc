/*************************************************** 
  This is an improved library for the FPM10/R305/ZFM20 optical fingerprint sensor
  Based on the Adafruit R305 library https://github.com/adafruit/Adafruit-Fingerprint-Sensor-Library
  
  Written by Brian Ejike <brianrho94@gmail.com> (2017)
  Distributed under the terms of the MIT license
 ****************************************************/

#include <machine/cortex/FPM.h>
#include <alarm.h>

static const char param_offsets[] = {0, 1, 2, 3, 4, 6, 7};
static const char param_sizes[] = {2, 2, 2, 2, 4, 2, 2};
static const uint16_t pLengths[] = {32, 64, 128, 256};

using namespace EPOS;
OStream fpmCout;

FPM::FPM() {
  thePassword = 0;
  theAddress = 0xFFFFFFFF;
  packetLen = 32;
  capacity = 0;
}

uint8_t FPM::handshake(){
  buffer[0] = FINGERPRINT_HANDSHAKE; 
  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 4, buffer);
  getReply();
  if (buffer[6] != FINGERPRINT_ACKPACKET)
   return FINGERPRINT_BADPACKET;
  return buffer[9];
}

bool FPM::begin(UART *ss, uint32_t password, uint32_t address, uint8_t pLen) {
    mySerial = ss;
    clearBuffer();
    
    buffer[0] = FINGERPRINT_VERIFYPASSWORD;
    buffer[1] = thePassword >> 24; buffer[2] = thePassword >> 16;
    buffer[3] = thePassword >> 8; buffer[4] = thePassword;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 7, buffer);

    uint16_t len = getReply();

    if ((len != 1) || (buffer[6] != FINGERPRINT_ACKPACKET) || (buffer[9] != FINGERPRINT_OK)){
       return false;
    }

    handshake();
    thePassword = password;
    theAddress = address;
 
    if (readParam(DB_SIZE, &capacity) != FINGERPRINT_OK)
        return false;       // get the capacity

    if (pLen <= PACKET_256){               // set the packet length as needed
      if (setParam(SET_PACKET_LEN, pLen) == FINGERPRINT_OK){
          packetLen = pLengths[pLen];
          return true;
      }
    }
    else {
      if (readParam(PACKET_LEN, &packetLen) == FINGERPRINT_OK){     // else get the present packet length
          packetLen = pLengths[packetLen];
          return true;
      }
    }
    return false;
}

uint8_t FPM::getImage(void) {
  buffer[0] = FINGERPRINT_GETIMAGE;
  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);
  getReply();
  
  if (buffer[6] != FINGERPRINT_ACKPACKET)
   return FINGERPRINT_BADPACKET;
  return buffer[9];
}

uint8_t FPM::image2Tz(uint8_t slot) {
  buffer[0] = FINGERPRINT_IMAGE2TZ; 
  buffer[1] = slot;
  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 4, buffer);
  getReply();

  if (buffer[6] != FINGERPRINT_ACKPACKET)
   return FINGERPRINT_BADPACKET;
  return buffer[9];
}


uint8_t FPM::createModel(void) {
  buffer[0] = FINGERPRINT_REGMODEL;
  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);
  getReply();
  
  if (buffer[6] != FINGERPRINT_ACKPACKET)
   return FINGERPRINT_BADPACKET;
  return buffer[9];
}


uint8_t FPM::storeModel(uint16_t id) {
  buffer[0] = FINGERPRINT_STORE;
  buffer[1] = 0x01;
  buffer[2] = id >> 8; buffer[3] = id & 0xFF;
  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 6, buffer);

  getReply();

  if (buffer[6] != FINGERPRINT_ACKPACKET)
   return FINGERPRINT_BADPACKET;
  return buffer[9];
}
    
//read a fingerprint template from flash into Char Buffer 1
uint8_t FPM::loadModel(uint16_t id) {
    buffer[0] = FINGERPRINT_LOAD;
    buffer[1] = 0x01;
    buffer[2] = id >> 8; buffer[3] = id & 0xFF;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 6, buffer);
    getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    return buffer[9];
}


uint8_t FPM::setParam(uint8_t param, uint8_t value){
	buffer[0] = FINGERPRINT_SETSYSPARAM;
    buffer[1] = param; buffer[2] = value;
	writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 5, buffer);
    getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    if (buffer[9] == FINGERPRINT_OK && param == SET_PACKET_LEN) // update packet length since we need it
        packetLen = pLengths[value];
    return buffer[9];
}

uint8_t FPM::readParam(uint8_t param, uint16_t * value){
    uint32_t val = *value;
    bool ret = readParam(param, &val);
    *value = val;
    return ret;
}

uint8_t FPM::readParam(uint8_t param, uint32_t * value){
    buffer[0] = FINGERPRINT_READSYSPARAM;
	  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);

    clearBuffer();
    getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    
    *value = 0;
    uint8_t * loc;
    if (buffer[9] == FINGERPRINT_OK){
        loc = &buffer[10] + param_offsets[param]*param_sizes[param];
        for (int i = 0; i < param_sizes[param]; i++){
            *((uint8_t *)value + i) = *(loc + param_sizes[param] - 1 - i);
        }
    }      
    return buffer[9];
}

// NEW: download fingerprint image to pc
uint8_t FPM::downImage(void){
	buffer[0] = FINGERPRINT_IMGUPLOAD;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);
    getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    return buffer[9];
}

bool FPM::readRaw(void * out, uint8_t outType, bool * lastPacket, uint16_t * bufLen){
    OStream * outStream;
    uint8_t * outBuf = 0;
    
    if (outType == ARRAY_TYPE) {
        if (bufLen == 0 || *bufLen < packetLen)        // if a buffer was provided, ensure theres enough space
            return false;
        else
            outBuf = (uint8_t *)out;
    }
    else if (outType == STREAM_TYPE)
        outStream = (OStream *)out;
    else
        return false;
    
    uint8_t chunk[packetLen + 12];
    uint8_t pid;
    
    uint16_t len;
    if (outType == ARRAY_TYPE)
        len = getReply(chunk);
    
    else if (outType == STREAM_TYPE)
        len = getReply(chunk, outStream);
    
    if (len != packetLen){
        return false;
    }
    
    pid = chunk[6];
    *lastPacket = false;
    
    if (pid == FINGERPRINT_DATAPACKET || pid == FINGERPRINT_ENDDATAPACKET){
        if (outType == ARRAY_TYPE){
            memcpy(outBuf, &chunk[9], len);
            *bufLen = len;
        }
        if (pid == FINGERPRINT_ENDDATAPACKET)
            *lastPacket = true;
        return true;
    }
    return false;
}

void FPM::writeRaw(uint8_t * data, uint16_t len){
    uint16_t written = 0;
    while (len > packetLen){
        writePacket(theAddress, FINGERPRINT_DATAPACKET, packetLen + 2, &data[written]);
        written += packetLen;
        len -= packetLen;
    }
    writePacket(theAddress, FINGERPRINT_ENDDATAPACKET, len + 2, &data[written]);
    getReply(); // sensor sends unkonwn data
  // //fpmCout << ("---------------------------------------------") << endl;
  // for (int i = 0; i < 768; ++i)
  // {
  //   //fpmCout << "0x" << hex << data[i] << ", ";
  // }
  // //fpmCout << ("--------------------------------------------") << endl;
}

bool FPM::getBufOneTemplate(uint8_t * templateBuf){
	uint16_t pos = 0, count = 0, buflen = TEMPLATE_SIZE; 
  bool lastPacket = false, working;
	while (true){
      while(!mySerial->ready_to_get());
      working = readRaw(templateBuf + pos, ARRAY_TYPE, &lastPacket, &buflen);
      if (working){
        count++;
        pos += buflen;
        buflen = TEMPLATE_SIZE - pos;
        if (lastPacket){
          return true;
      }
    }
      else {
 		return false;
    }
  }
}

//transfer a fingerprint template from Char Buffer 1 to host computer
uint8_t FPM::getModel(void) {
    buffer[0] = FINGERPRINT_UPLOAD;
    buffer[1] = 0x01;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 4, buffer);
    getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    return buffer[9];
}

uint8_t FPM::uploadModel(void){
    buffer[0] = FINGERPRINT_DOWNCHAR;
    buffer[1] = 0x01;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 4, buffer);
    getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    return buffer[9];

}
    
uint8_t FPM::deleteModel(uint16_t id, uint16_t num) {
    buffer[0] = FINGERPRINT_DELETE;
    buffer[1] = id >> 8; buffer[2] = id & 0xFF;
    buffer[3] = num >> 8; buffer[4] = num & 0xFF;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 7, buffer);
    getReply();
        
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    return buffer[9];
}

uint8_t FPM::emptyDatabase(void) {
  buffer[0] = FINGERPRINT_EMPTY;
  writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);
  getReply();
  
  if (buffer[6] != FINGERPRINT_ACKPACKET)
   return FINGERPRINT_BADPACKET;
  return buffer[9];
}

uint8_t FPM::fingerFastSearch(void) {
    fingerID = 0xFFFF;
    confidence = 0xFFFF;
    // high speed search of slot #1 starting at page 0x0000 and page #0x0671 
    buffer[0] = FINGERPRINT_HISPEEDSEARCH;
    buffer[1] = 0x01;
    buffer[2] = 0x00; buffer[3] = 0x00;
    buffer[4] = capacity >> 8; buffer[5] = capacity & 0xFF;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 8, buffer);
    getReply();

    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;

    if (buffer[9] == FINGERPRINT_OK){
        fingerID = buffer[10];
        fingerID <<= 8;
        fingerID |= buffer[11];

        confidence = buffer[12];
        confidence <<= 8;
        confidence |= buffer[13];
    }

    return buffer[9];
}

uint8_t FPM::match_pair(void){
    buffer[0] = FINGERPRINT_PAIRMATCH;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);
    getReply();

    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    
    if (buffer[9] == FINGERPRINT_OK){
        confidence = buffer[10]; 
        confidence <<= 8;
        confidence |= buffer[11];
    }

    return buffer[9];
}

uint8_t FPM::getTemplateCount(void) {
    templateCount = 0xFFFF;
    // get number of templates in memory
    buffer[0] = FINGERPRINT_TEMPLATECOUNT;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 3, buffer);
    getReply();

    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    
    if (buffer[9] == FINGERPRINT_OK){
        templateCount = buffer[10];
        templateCount <<= 8;
        templateCount |= buffer[11];
    }

    return buffer[9];
}

bool FPM::getFreeIndex(int16_t * id){
  int p = -1;
  for (int page = 0; page < (this->capacity / TEMPLATES_PER_PAGE) + 1; page++){
    p = getFreeIndexPage(page, id);
    switch (p){
      case FINGERPRINT_OK:
        if (*id != FINGERPRINT_NOFREEINDEX){
          return true;
        }
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        return false;
      default:
        break;
    }
  }
  return false;
}

uint8_t FPM::getFreeIndexPage(uint8_t page, int16_t * id){
    buffer[0] = FINGERPRINT_READTEMPLATEINDEX; 
    buffer[1] = page;
    writePacket(theAddress, FINGERPRINT_COMMANDPACKET, 4, buffer);
    uint16_t len = getReply();
    
    if (buffer[6] != FINGERPRINT_ACKPACKET)
        return FINGERPRINT_BADPACKET;
    if (buffer[9] == FINGERPRINT_OK){
        for (int i = 0; i < len; i++){
            if (buffer[10+i] < 0xff){
                uint8_t index = 0;
                for (uint8_t bit = 0x01; bit != 0; bit <<= 1){
                    if ((bit & buffer[10+i]) == 0){
                        *id = (256 * page) + (i * 8) + index;
                        return buffer[9];
                    }
                    index++;
                }
            }
        }
    }
    *id = FINGERPRINT_NOFREEINDEX;  // no free space found
    return buffer[9];
}

bool FPM::sendImageSerial(){
	int i = 0;
  bool lastPacket = false, working;
	while (true){
	  i++;
      while(!mySerial->ready_to_get());
      working = readRaw(&fpmCout, STREAM_TYPE, &lastPacket);
      if (working){
        if (lastPacket){
          //fpmCout << "Num Iteracoes utimo pacote: " << i << endl;
          return true;
      }
    }
      else {
      	//fpmCout << "Num Iteracoes: " << i << endl;
 		return false;
    }
  }	
}

void FPM::writePacket(uint32_t addr, uint8_t packettype, 
				       uint16_t len, uint8_t *packet) {
#ifdef FINGERPRINT_DEBUG
  ////fpmCout << ("---> 0x");
  //fpmCout << hex << (uint32_t)(FINGERPRINT_STARTCODE >> 8) << endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)FINGERPRINT_STARTCODE<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)(addr >> 24)<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)(addr >> 16)<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)(addr >> 8)<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)(addr)<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)packettype<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)(len >> 8)<< endl;
  ////fpmCout << (" 0x");
  //fpmCout << hex << (uint32_t)(len) << endl;
#endif


  uint16_t sum = (len>>8) + (len&0xFF) + packettype;
  for (uint8_t i=0; i< len-2; i++) {
    sum += packet[i];
  }

while(!mySerial->ready_to_put());
mySerial->put((uint8_t)(FINGERPRINT_STARTCODE >> 8));
mySerial->put((uint8_t)FINGERPRINT_STARTCODE);
mySerial->put((uint8_t)(addr >> 24));
mySerial->put((uint8_t)(addr >> 16));
mySerial->put((uint8_t)(addr >> 8));
mySerial->put((uint8_t)(addr));
mySerial->put((uint8_t)packettype);
mySerial->put((uint8_t)(len >> 8));
mySerial->put((uint8_t)(len));
for (uint8_t i=0; i< len-2; i++) {
  mySerial->put((uint8_t)(packet[i]));
}
mySerial->put((uint8_t)(sum>>8));
mySerial->put((uint8_t)sum);



}

//MODIFIED: adjusted to allow for image download
uint16_t FPM::getReply(uint8_t * replyBuf, OStream * outStream, uint16_t timeout) {
  uint8_t idx, val, *packet;
  uint16_t timer = 0;
  uint16_t len = 0;
  uint16_t expectedSum = 0;
  uint16_t receivedSum = 0;
  
  if (replyBuf != 0)
      packet = replyBuf;
  else
      packet = buffer;
  
  idx = 0;

while (true) {
    while (!mySerial->ready_to_get()) {
      Delay(5000);
      timer++;
      if (timer >= timeout){
          fpmCout << "TIMEOUT!" << endl;
          fpmCout << "Bytes lidos: "<< idx <<endl;
          return FINGERPRINT_TIMEOUT;        
      }

    }
    // something to read!
    val = mySerial->get();
    
    if (idx > 8 && outStream != 0){
    	packet[idx] = val;
        (*outStream) << (char) val;
    }
    else  
        packet[idx] = val;

    if ((idx == 0) && (packet[0] != (FINGERPRINT_STARTCODE >> 8)))
      continue;
    
#ifdef FINGERPRINT_DEBUG
    ////fpmCout << (" 0x"); 
    ////fpmCout << reply[idx] << endl;
#endif

    if (idx == 8) {

      if ((packet[0] != (FINGERPRINT_STARTCODE >> 8)) ||
          (packet[1] != (FINGERPRINT_STARTCODE & 0xFF)))
          return FINGERPRINT_BADPACKET;
      len = packet[7];
      len <<= 8;
      len |= packet[8];
      len -= 2;  // ignore checksum
    }
    
    idx++;
    if (idx < len + 9){
        continue;
    } else {
        receivedSum = mySerial->get() << 8;   // read off checksum if its arrived, to free up buffer space
        receivedSum |= mySerial->get();
        for (int i = 6; i < len + 9; ++i)
        {
          expectedSum += packet[i]; //read about checksum on datasheet
        }
        if(expectedSum == receivedSum)
          return len;
        else{
          fpmCout << "Checksum nao é a esperada" << endl;
          fpmCout << "Checksum reply: "<< hex << receivedSum << endl;
          fpmCout << "Checksum calculada: "<< hex << expectedSum << endl;
          printBuffer(packet);
          packet[6] = FINGERPRINT_BADPACKET;
          return FINGERPRINT_BADPACKET;
        }
    }
  }
}

void FPM:: printBuffer(uint8_t * buffer, uint32_t size){
    for(uint32_t i = 0; i < size; i++){
        fpmCout << hex << buffer[i] << endl;
    }
}

void FPM::clearBuffer(){
    for(int i = 0; i < 44; i++){
        buffer[i] = 255;
    }
}

void FPM::printAllParams(){
    uint32_t value;

    clearBuffer();
    fpmCout << "Lendo params" << endl;
    if (readParam(STATUS_REG, &value) == FINGERPRINT_OK){
      fpmCout << "Status: " << value << endl;
    } 
    if (readParam(STATUS_REG, &value) == FINGERPRINT_OK){
      fpmCout << "System ID: " << value << endl;
    }
    if (readParam(DB_SIZE, &value) == FINGERPRINT_OK){
      fpmCout << "Database Size: " << value << endl;
    }
    if (readParam(SEC_LEVEL, &value) == FINGERPRINT_OK){
      fpmCout << "Security Level: " << value << endl;
    }   
    if (readParam(DEVICE_ADDR, &value) == FINGERPRINT_OK){
      fpmCout << "Device Address: " << value << endl;
    }   
    if (readParam(PACKET_LEN, &value) == FINGERPRINT_OK){
      packetLen = pLengths[value];
      fpmCout << "Packet Size: " << value << endl;
    }
    if (readParam(BAUD_RATE, &value) == FINGERPRINT_OK){
      fpmCout << "Baud Rate: " << value*9600 << endl;
    }      
}



