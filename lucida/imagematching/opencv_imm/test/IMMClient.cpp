#include <unistd.h>
#include <gflags/gflags.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <folly/futures/Future.h>
#include "gen-cpp2/LucidaService.h"
#include <thrift/lib/cpp2/async/HeaderClientChannel.h>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

#include "client/dbclient.h" // MongoDB

using namespace folly;
using namespace apache::thrift;
using namespace apache::thrift::async;
using namespace cpp2;
using namespace std;

using mongo::ConnectionString;
using mongo::DBClientBase;
using mongo::BSONObj;
using mongo::BSONObjBuilder;

namespace fs = boost::filesystem;

DEFINE_int32(port,
		8082,
		"Port for IMM service (default: 8082)");

DEFINE_string(hostname,
		"127.0.0.1",
		"Hostname of the server (default: localhost)");

void saveToMongoDb(const string &LUCID,
		const string &label, const string &data) {
	string uri = "localhost:27017"; // specify where MongoDB is running
	string errmsg;
	ConnectionString cs = ConnectionString::parse(uri, errmsg);
	if (!cs.isValid()) {
		throw runtime_error("Error parsing connection string "
				+ uri + ": " + errmsg);
	}
	unique_ptr<DBClientBase> conn(cs.connect(errmsg));
	if (!conn) {
		throw runtime_error("Couldn't connect: " + errmsg);
	}
	BSONObj p = BSONObjBuilder().append("label", label).append("data", data)
			.append("size", (int) data.size()).obj();
	conn->insert("lucida.images_" + LUCID, p); // insert the image data
	string e = conn->getLastError();
	if (!e.empty()) {
		throw runtime_error("Insert failed " + e);
	}
}

string getImageData(const string &image_path) {
	ifstream fin(image_path.c_str(), ios::binary);
	ostringstream ostrm;
	ostrm << fin.rdbuf();
	string image(ostrm.str());
	if (!fin) {
		cerr << "Could not open the file " << image_path << endl;
		exit(1);
	}
	return image;
}

int main(int argc, char* argv[]) {
	google::InitGoogleLogging(argv[0]);
	google::ParseCommandLineFlags(&argc, &argv, true);
	EventBase event_base;
	std::shared_ptr<apache::thrift::async::TAsyncSocket> socket_t(
			TAsyncSocket::newSocket(&event_base, FLAGS_hostname, FLAGS_port));
	LucidaServiceAsyncClient client(
			std::unique_ptr<HeaderClientChannel, DelayedDestruction::Destructor>(
					new HeaderClientChannel(socket_t)));
	// Open the images.
	string db = fs::current_path().string() + "/test_db";
	fs::path p = fs::system_complete(db);
	assert(fs::is_directory(p));
	fs::directory_iterator end_iter;
	for(fs::directory_iterator dir_itr(p); dir_itr != end_iter; ++dir_itr) {
		string image = getImageData(dir_itr->path().string());
		// Create a QuerySpec.
		QuerySpec query_spec;
		// Create a QueryInput for each image and add it to the QuerySpec.
		QueryInput query_input;
		query_input.type = "image";
		query_input.data.push_back(image);
		string label = dir_itr->path().stem().string();
		cout << "Label: " << label << endl;
		cout << "Image size: " << image.size() << endl;
		query_input.tags.push_back(label);
		query_spec.content.push_back(query_input);
		saveToMongoDb("Johann", label, image);
		// Make request.
		client.future_learn("Johann", std::move(query_spec)).then(
				[](folly::Try<folly::Unit>&& t) mutable {
			cout << "Done" << endl;
		});
		cout << "Going to loop" << endl;
		event_base.loop();
	}

	// Infer.
	// Make request.
	int num_tests = 3;
	if (argc == 2) {
		num_tests = atoi(argv[1]);
	}
	for (int i = 0; i < num_tests; ++i) {
		string image = getImageData("test" + to_string(i) + ".jpg");
		// Create a QuerySpec.
		QuerySpec query_spec;
		// Create a QueryInput for the query image and add it to the QuerySpec.
		QueryInput query_input;
		query_input.type = "image";
		query_input.data.push_back(image);
		query_spec.content.push_back(query_input);
		cout << i << " Sending request to IMM at 8082" << endl;
		auto result = client.future_infer("Johann", std::move(query_spec)).then(
				[=](folly::Try<std::string>&& t) mutable {
			cout << i << " result: " << t.value() << endl;
			return t.value();
		});
	}
	cout << "Going to loop" << endl;
	event_base.loop();
	return 0;
}
