#include "session.h"
#include "HttpRequest.h"
#include "http_response.h"

#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>

session::session(boost::asio::io_service& io_service,
  std::map <std::string, std::string> function_mapping)
  : socket_(io_service), function_mapping(function_mapping) {}

tcp::socket& session::socket()
{
  return socket_;
}

void session::start()
{
  std::cout << "========= Starting session =========" << std::endl;
  
  boost::asio::async_read_until(socket_, buffer, "\r\n\r\n",
      boost::bind(&session::handle_request, this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
}

int session::handle_request(const boost::system::error_code& error,
    size_t bytes_transferred){

  std::vector<char>message_request = convert_buffer();
  HttpRequest request(message_request);
  http_response response;

  std::string url = request.getUrl();  
  std::string function = get_function_from_url(url);
  std::cout << "Function: " << function << std::endl;

  if (!error){
    if (function == "echo_dir")
      handle_write(error, bytes_transferred);
    else if (function == "static_dir") {
      std::string abs_path = get_exec_path() + "/public" + get_path_from_url(url);
      std::cout << "Absolute Path: " << abs_path << std::endl;
      
      if (!file_exists(abs_path)) {
        // TODO: 404 Error
        std::cerr << "Error: " << abs_path << " does not exist" << std::endl;
        return -1;
      } 

      // save content_type header based on requested file extension
      std::string content_type = get_content_type(abs_path);
      
      // raw byte array
      std::vector<char> to_send = read_file(abs_path);
      
      std::string status = "200 OK"; 
      response.set_status(status);

      std::string length_header = "Content-length: " + std::to_string(to_send.size());
      response.add_header(length_header);
      std::string type_header = "Content-type: " + content_type;
      response.add_header(type_header); 

      response.set_body(to_send); 

      // write out response
      write_string(response.to_string());
    }
    else {
      // TODO: error case send error response
    }

  } 
  else{
    std::cerr << error.message() << std::endl;
    return -1;
  }
  return 0;

}

// gets current path of executable
std::string session::get_exec_path() {
    // max path is 2048 characters in file directory
    const int MAX_PATH = 2048;
    char buffer[MAX_PATH];
    if (getcwd(buffer, sizeof(buffer)) != NULL) {
        return std::string(buffer);
    }    
    else {
        std::cerr << "Error: unable to find current working directory" << std::endl;
    }
    return "";
}

// checks if file exists
bool session::file_exists(std::string filename) {
    struct stat buffer;   
    return (stat(filename.c_str(), &buffer) == 0); 
}

// gets content-type based on the file extension of requested file
std::string session::get_content_type(std::string filename) {
    unsigned int i;
    
    // search for either last period or last slash in filename
    for (i = filename.size() - 1; i >= 0; i--) {
        // file with no extension type
        if (filename[i] == '/') {
            return "text/plain";
        } else if (filename[i] == '.') { // found pos of beginning of extension
            break;
        }
    }

    std::string ext = filename.substr(i + 1, std::string::npos);
    std::string content_type;
    
    // based on ext decide content_type header
    if (ext == "html") {
        content_type = "text/html";
    } else if (ext == "jpg") {
        content_type = "image/jpeg";
    } else if (ext == "pdf") {
        content_type = "application/pdf";
    } else {
        content_type = "text/plain";
    }
    return content_type;
}


// reads raw file into vector of characters
std::vector<char> session::read_file(std::string filename)
{
    std::ifstream ifs(filename, std::ios::binary|std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();

    std::vector<char>  result(pos);

    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);

    return result;
}

// given a string, writes out to socket and ends connection
void session::write_string(std::string send) {
    
  std::cout << "========= Writing out =========" << std::endl;

  // output buffer
  boost::asio::streambuf out_streambuf;
  std::ostream out(&out_streambuf);
  out << send;  

  boost::asio::write(socket_, out_streambuf);
  
  // wait for transmission Note: this could hang forever
  tcdrain(socket_.native_handle());

  // close socket
  boost::system::error_code ec;
  socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
  socket_.close();
  
  std::cout << "========= Ending Session =========" << std::endl;
}

int session::handle_write(const boost::system::error_code& error,
    size_t bytes_transferred)
{

  std::cout << "========= Writing out =========" << std::endl;

  // output buffer
  boost::asio::streambuf out_streambuf;
  
  //sets up the output buffer with the correct headers
  setup_obuffer(out_streambuf, bytes_transferred);
  
  if (!error)
  {
    boost::asio::write(socket_, out_streambuf);
    
    // wait for transmission Note: this could hang forever
    tcdrain(socket_.native_handle());

    // close socket
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close();
    
    std::cout << "========= Ending Session =========" << std::endl;
  }
  else
  {
    std::cerr << error.message() << std::endl;
    return -1;
  }
  return 0;
}

void session::setup_obuffer(boost::asio::streambuf& out_streambuf, size_t bytes_transferred)
{
  std::ostream out(&out_streambuf);
  // setup headers
  out << "HTTP/1.1 200 OK\r\n";
  out << "Content-Type: text/plain\r\n";
  // minus 4 for the double carriage return
  out << "Content-Length: " << bytes_transferred - 4 << "\r\n\r\n";
  
  // echo message under headers
  out << &buffer;
}

std::vector<char> session::convert_buffer()
{
  std::vector<char>converted_vector;

  std::string s{
    buffers_begin(buffer.data()),
    buffers_end(buffer.data())
  };
  
  std::copy(s.begin(), s.end(), std::back_inserter(converted_vector));
  return converted_vector;
}

std::string session::get_path_from_url(std::string url) {
  // Assumption: url must be prefixed with "/static/" at this point 
  //             get_function_from_url has already been called on url
  int second_slash_pos = url.find("/", 1);
  // from second slash to end of string is path
  std::string path = url.substr(second_slash_pos, std::string::npos);

  return path;
}

std::string session::get_function_from_url(std::string url)
{
  std::string function = "";
  // TODO: log these errors
  if (url.length() == 0)
    return "Error: Blank Function Field";
  if (url.length() == 1 && url == "/")
    return "/";

  int second_slash_pos = url.find("/", 1);
  // string between first and second slashs
  std::string dir = url.substr(0, second_slash_pos);

  std::map<std::string, std::string>::iterator it = function_mapping.find(dir);
  // if valid function found
  if (it != function_mapping.end()){
     function = it->second;
  }
  else{
    // TODO: log these errors
    function = "Error in determining function";
  }
  return function;
}

