#include "../include/ct.h"

#include <fstream>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static const char nibble[] = "0123456789abcdef";

static std::string HexString(const bstring &data) {
  std::string ret;
  for (unsigned int i = 0; i < data.size(); ++i) {
    ret.push_back(nibble[(data[i] >> 4) & 0xf]);
    ret.push_back(nibble[data[i] & 0xf]);
  }
  return ret;
}

void unknownCommand(const std::string &cmd) {
  std::cerr << "Unknown command: " << cmd << '\n';
}

void readAll(std::string *contents, std::ifstream &in) {
  for ( ; ; ) {
    char buf[1024];

    size_t n = in.read(buf, sizeof buf).gcount();
    assert(!in.fail() || in.eof());
    assert(n >= 0);
    contents->append(buf, n);
    if (in.eof())
      return;
  }
}

class CTResponse {
public:
  byte code;
  bstring data;
};

// Provisional packet format
// struct {
//  uint8 version;
//  uint8 command;
//  uint24 length;
//  opaque fragment[ClientCommand.length];
// } ClientCommand;

class CTClient {
public:
  CTClient(const char *server, unsigned port) {
    fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) {
      perror("Socket creation failed");
      exit(3);
    }

    static struct sockaddr_in serverSocket;
    memset(&serverSocket, 0, sizeof serverSocket);
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_port = htons(port);
    if (inet_aton(server, &serverSocket.sin_addr) != 1) {
      std::cerr << "Can't parse server address: " << server << '.' << std::endl;
      exit(5);
    }

    std::cout << "Connecting to " << server << ':' << port << '.' << std::endl;
    int ret = connect(fd_, (struct sockaddr *)&serverSocket,
		      sizeof serverSocket);
    if (ret < 0) {
      close(fd_);
      perror("Connect failed");
      exit(4);
    }
  }
  // struct {
  //   opaque bundle[ClientCommand.length];
  // } ClientCommandUploadBundle;
  void uploadBundle(const std::string &bundle) {
    writeCommand(ct::UPLOAD_BUNDLE, bundle.length());
    write(bundle.data(), bundle.length());
    CTResponse response;
    readResponse(&response);
  }

private:
  void write(const void *buf, size_t length) const {
    for (size_t offset = 0; offset < length; ) {
      int n = ::write(fd_, ((char *)buf) + offset, length - offset);
      assert(n > 0);
      offset += n;
    }
  }

  void writeByte(size_t b) const {
    char buf[1];
    buf[0] = b;
    write(buf, 1);
  }

  // Write MSB first.
  void writeLength(size_t length, size_t lengthOfLength) const {
    assert(lengthOfLength <= sizeof length);
    assert(length < 1U << (lengthOfLength * 8));
    for ( ; lengthOfLength > 0; --lengthOfLength) {
      size_t b = length & (0xff << ((lengthOfLength - 1) * 8));
      writeByte(b >> ((lengthOfLength - 1) * 8));
    }
  }

  void read(void *buf, size_t length) const {
    for (size_t offset = 0; offset < length; ) {
      int n = ::read(fd_, ((char *)buf) + offset, length - offset);
      assert(n > 0);
      offset += n;
    }
  }

  byte readByte() const {
    byte buf[1];
    read(buf, 1);
    return buf[0];
  }

  size_t readLength(size_t lengthOfLength) const {
    byte buf[lengthOfLength];
    read(buf, lengthOfLength);
    size_t length = 0;
    for (size_t n = 0; n < lengthOfLength; ++n)
      length = (length << 8) + buf[n];
    return length;
  }

  void readString(bstring *dst, size_t length) const {
    byte buf[length];
    read(buf, length);
    dst->assign(buf, length);
  }

  void writeCommand(ct::ClientCommand cmd, size_t length) const {
    writeByte(VERSION);
    writeByte(cmd);
    writeLength(length, 3);
  }

  void readResponse(CTResponse *response) {
    byte version = readByte();
    assert(version == VERSION);
    response->code = readByte();
    size_t length = readLength(3);
    readString(&response->data, length);
    std::cout << "Response code is " << (int)response->code << ", data length "
	      << length << std::endl;
    if (response->code == ct::SUBMITTED)
      std::cout << "Token is " << HexString(response->data) << std::endl;
  }

  int fd_;

  static const byte VERSION = 0;
};

void uploadBundle(int argc, const char **argv) {
  if (argc < 4) {
    std::cerr << argv[0] << " <file> <server> <port>\n";
    exit(2);
  }
  const char *file = argv[1];
  const char *serverName = argv[2];
  unsigned port = atoi(argv[3]);

  std::cout << "Uploading certificate bundle from " << file << '.' << std::endl;

  // FIXME: do some kind of sanity check on the contents?
  std::ifstream in(file);
  if (!in.is_open()) {
    perror(file);
    exit(6);
  }

  std::string contents;
  readAll(&contents, in);

  std::cout << file << " is " << contents.length() << " bytes." << std::endl;

  CTClient client(serverName, port);
  client.uploadBundle(contents);
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    std::cerr << argv[0] << " <command> ...\n";
    return 1;
  }

  const std::string cmd(argv[1]);
  if (cmd == "upload")
    uploadBundle(argc - 1, argv + 1);
  else
    unknownCommand(cmd);
}
