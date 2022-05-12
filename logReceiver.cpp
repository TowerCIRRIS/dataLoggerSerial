/*
 * logReceiver.cpp
 *
 *  Created on: 9 mai 2022
 *      Author: teamat
 */

#include "logReceiver.h"
#include "serialComm.h"
#include "dataLogger.h"


#include <chrono>
#include <sys/time.h>
#include <ctime>


using namespace std::chrono;


logReceiver::logReceiver(uint32_t maxPacketLenght, uint32_t maxDataBuffer) {

	mMaxPacketLen =maxPacketLenght;
	mInBuffer = new uint8_t[maxPacketLenght];
	mMaxDataBuffer = maxDataBuffer;
	mDataIn = new uint8_t[maxDataBuffer];

	comm = new atComm(maxPacketLenght);



}

logReceiver::~logReceiver() {

	delete[] mInBuffer;
	delete[] mDataIn;

	delete comm;
}



bool logReceiver::openComm(const char *comPort, uint32_t baudRate) {

	serialHandle = open_serial_port(comPort, baudRate);
	if (serialHandle == NULL) {
		std::cout << "Error opening port "<< std::endl;
		return false;
	} else {
		if (serialHandle == INVALID_HANDLE_VALUE) {
			std::cout << "Error opening port "<< std::endl;
			return false;
		} else {
			std::cout << "Success:  port created\n\r\r "<< std::endl;
		}
	}

	reset();

	return true;
}


void logReceiver::endComm() {

	CloseHandle(serialHandle);
}


bool logReceiver::reset()
{
	comm->resetBuffer();

	// Envoyer la commande pour �recevoir le data
	flushPort(serialHandle);
	write_port(serialHandle, (uint8_t*)"\r", 1); // in case something was typed before
	Sleep(100);
	flushPort(serialHandle);

	bool dataToReceive = true;
	char inCharacter[10];
	SSIZE_T readSize;

	while (dataToReceive) {

		readSize = read_port(serialHandle, (uint8_t*) inCharacter, 1);

	if (readSize <= 0) {

		dataToReceive = false;
	}

	dataIndex = 0;

}

	return true;
}


int logReceiver::refreshFileList()
 {
	string getFilesCommand = string("listfiles\r");

	string delimiter = "\n\r";
	SSIZE_T readSize;
	bool dataToReceive = true;
	char inCharacter[10];

	while (dataToReceive) {

		readSize = read_port(serialHandle, (uint8_t*) inCharacter, 1);

		if (readSize <= 0) {

			dataToReceive = false;
		}

	}

	flushPort(serialHandle);
	write_port(serialHandle, (uint8_t*) getFilesCommand.c_str(), getFilesCommand.length());

	Sleep(100);




	string receivedData;

	dataToReceive = true;
	while (dataToReceive) {

		readSize = read_port(serialHandle, (uint8_t*) inCharacter, 1);

		if (readSize > 0) {
			receivedData.append(inCharacter);
		}
		else
		{
			dataToReceive = false;
		}

	}

	size_t pos = 0;
	std::string token;

	//mFileCount = 0;
	while ((pos = receivedData.find(delimiter)) != std::string::npos) {
	    token = receivedData.substr(0, pos);

	    if(token.find(".dat") != std::string::npos)
	    {
	    	mFileList.push_back(token);
	    	//mFileCount++;
	    }

	    receivedData.erase(0, pos + delimiter.length());
	}



	return mFileList.size();
}

int logReceiver::getFileListCount()
{
	return mFileList.size();
}

int logReceiver::getFileName(int fileSelect,char* fileName)
{
	if(mFileList.size() > 0 && fileSelect < mFileList.size())
	{
		strcpy(fileName, mFileList[fileSelect].c_str());
		return mFileList[fileSelect].length();
	}

	return -1;
}



bool logReceiver::downLoadData(const char* filename)
{

	if (string(filename).find(".dat") == std::string::npos)
	{
		cout << "Error: wrong input file extension"<< std::endl;
		return false;
	}

	string downloadCommand = string("downloaddata ") + string(filename) + string("\n");

	flushPort(serialHandle);
	write_port(serialHandle, (uint8_t*)downloadCommand.c_str(), downloadCommand.length());

	// R�ceptionn des donn�es
	ofstream outBinaryfile;

	outBinaryfile.open(filename,std::ios::binary);
	if(outBinaryfile.is_open())
	{
		cout << "Data receive, open file"<< std::endl;
	}
	else
	{
		cout << "ERROR: Unable to open output file"<< std::endl;
		return false;
	}

	reset(); // reset pour le nouveau paquet;

	uint32_t packetNumber = 0;
	uint32_t rxPacketNumber = 0;
	uint32_t retryCount = 0;

	uint64_t startTimeMs = getTimeMs();

	SSIZE_T readSize;

	bool dataToReceive = true;

	while(dataToReceive)
	{
		//Recevoir Data in indique longueur
		readSize = read_port(serialHandle, mInBuffer, mMaxPacketLen);

		if (readSize > 0) {
			startTimeMs = getTimeMs();
			for (int i = 0; i < readSize; i++) {
				int status = comm->addReceivedBytes(&mInBuffer[i], 1); //todo fix le cas ou les bytes d�bordent

				if (status < 0) {
					cout << "Rx error getting bytes: " << status<< std::endl;
					comm->resetBuffer();
					retryCount++;

					if (retryCount >= MAX_RETRY) {
						cout << "Max retry reached, aborting.. Error: " << status<< std::endl;
						dataToReceive = false;
						outBinaryfile.close();
						//fileOpened = false;
						return false;
					}

					break; // exit for

				} else {
					if (comm->packetIsComplete()) {
						retryCount = 0; // Valid message received, clear retry counter for next errors
						int packetDataCount = comm->getDataCount();
						if (packetDataCount) {
							dataInfo_t tempDataInfo;

							bool sendAck = false;
							for (int packetSelect = 0; packetSelect < packetDataCount; packetSelect++) {

								// Ajoute le data au buffer.
								comm->getDataInfo(packetSelect, &tempDataInfo);

								if (tempDataInfo.dataType == SDLOGGER_COMM_DATA_TYPE_MESSAGE_COUNT) {
									comm->getData(packetSelect, &rxPacketNumber, sizeof(rxPacketNumber));

									if (rxPacketNumber != packetNumber) {
										cout << "Packet missmatch, aborting.. packet number:" << packetNumber << std::endl;
										dataToReceive = false;
										outBinaryfile.close();
										//fileOpened = false;
										return false;
									}
									sendAck = true;

								} else if (tempDataInfo.dataType == SDLOGGER_COMM_DATA_TYPE_BINARYLOGDATA) {
									int dataLen = comm->getData(packetSelect, &mDataIn[dataIndex], mMaxDataBuffer - dataIndex);

									if (dataLen >= 0) {
										dataIndex += dataLen;
										packetNumber++;
									} else {
										cout << "Data Length error, aborting.."<< std::endl;
										dataToReceive = false;
										outBinaryfile.close();
										//fileOpened = false;
										return false;
									}
								} else if (tempDataInfo.dataType == SDLOGGER_COMM_DATA_TYPE_EMPTY) {
									//Nothing for now
								} else {
									cout << "Data type error, aborting.."<< std::endl;
									dataToReceive = false;
									outBinaryfile.close();
									//fileOpened = false;
									return false;
								}
							}

							if (sendAck) {
								write_port(serialHandle, (uint8_t*) "\r", 1); //ACK
							}

						} else {

						}

						// Regarder si c'est le dernier paquet du groupe de paquets � recevoir.
						if (comm->getLastPacketStatus()) {
							dataToReceive = false;
						}

						comm->resetBuffer(); // reset pour le prochain paquet au besoin;
					}
				}

			}
		} else {
			if (getTimeMs() - startTimeMs > 3000) {
				cout << "ERROR: Data reception timeout, aborting.."<< std::endl;
				dataToReceive = false;
				outBinaryfile.close();
				//fileOpened = false;
				return false;
			}
		}
	}


	cout << "Writing file"<< std::endl;
	outBinaryfile.write((char*)mDataIn,dataIndex);
	outBinaryfile.close();
	Sleep(100);

	string outAsciiFile = filename;

	std::size_t extensionPos = outAsciiFile.find(".dat");
	if (extensionPos!=std::string::npos)
	{
		outAsciiFile.replace(extensionPos,4,".csv");

		datToCsv(filename,outAsciiFile.c_str(),false);
	}


	return true;
}

uint64_t logReceiver::getTimeMs()
{
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
