#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/importer/importer.h"
#include "ipfs/merkledag/merkledag.h"
#include "ipfs/repo/fsrepo/fs_repo.h"
#include "ipfs/unixfs/unixfs.h"

#define MAX_DATA_SIZE 262144 // 1024 * 256;

/***
 * Imports OS files into the datastore
 */

/**
 * read the next chunk of bytes, create a node, and add a link to the node in the passed-in node
 * @param file the file handle
 * @param node the node to add to
 * @returns number of bytes read
 */
size_t ipfs_import_chunk(FILE* file, struct Node* parent_node, struct FSRepo* fs_repo, size_t* total_size) {
	unsigned char buffer[MAX_DATA_SIZE];
	size_t bytes_read = fread(buffer, 1, MAX_DATA_SIZE, file);

	// create a new node
	struct Node* new_node = NULL;
	ipfs_node_new_from_data(buffer, bytes_read, &new_node);
	// persist
	ipfs_merkledag_add(new_node, fs_repo);
	// put link in parent node
	struct NodeLink* new_link = NULL;
	ipfs_node_link_create("", new_node->hash, new_node->hash_size, &new_link);
	new_link->t_size = new_node->data_size;
	*total_size += new_link->t_size;
	ipfs_node_add_link(parent_node, new_link);
	ipfs_node_free(new_node);
	// save the parent_node if it is time...
	if (bytes_read != MAX_DATA_SIZE) {
		// build UnixFS for file
		struct UnixFS* unix_fs;
		if (ipfs_unixfs_new(&unix_fs) == 0) {
			return 0;
		}
		unix_fs->data_type = UNIXFS_FILE;
		unix_fs->file_size = *total_size;
		// now encode unixfs and put in parent_node->data
		size_t temp_size = ipfs_unixfs_protobuf_encode_size(unix_fs);
		unsigned char temp[temp_size];
		size_t bytes_written;
		if (ipfs_unixfs_protobuf_encode(unix_fs, temp, temp_size, &bytes_written) == 0) {
			ipfs_unixfs_free(unix_fs);
			return 0;
		}
		parent_node->data_size = bytes_written;
		parent_node->data = (unsigned char*)malloc(bytes_written);
		if (parent_node->data == NULL) {
			ipfs_unixfs_free(unix_fs);
			return 0;
		}
		memcpy(parent_node->data, temp, bytes_written);
		// persist the main node
		ipfs_merkledag_add(parent_node, fs_repo);
	}
	return bytes_read;
}

/**
 * Creates a node based on an incoming file
 * @param file_name the file to import
 * @param parent_node the root node (has links to others)
 * @returns true(1) on success
 */
int ipfs_import_file(const char* fileName, struct Node** parent_node, struct FSRepo* fs_repo) {
	int retVal = 1;
	int bytes_read = MAX_DATA_SIZE;
	size_t total_size = 0;

	FILE* file = fopen(fileName, "rb");
	retVal = ipfs_node_new(parent_node);
	if (retVal == 0)
		return 0;

	// add all nodes
	while ( bytes_read == MAX_DATA_SIZE) {
		bytes_read = ipfs_import_chunk(file, *parent_node, fs_repo, &total_size);
	}
	fclose(file);

	return 1;
}

/**
 * called from the command line
 * @param argc the number of arguments
 * @param argv the arguments
 */
int ipfs_import(int argc, char** argv) {
	/*
	 * Param 0: ipfs
	 * param 1: add
	 * param 2: filename
	 */
	struct Node* directory_node = NULL;
	struct FSRepo* fs_repo = NULL;

	// open the repo
	int retVal = ipfs_repo_fsrepo_new(NULL, NULL, &fs_repo);
	if (retVal == 0) {
		return 0;
	}
	retVal = ipfs_repo_fsrepo_open(fs_repo);

	// import the file(s)
	retVal = ipfs_import_file(argv[2], &directory_node, fs_repo);

	// give some results to the user
	int buffer_len = 100;
	unsigned char buffer[buffer_len];
	retVal = ipfs_cid_hash_to_base58(directory_node->hash, directory_node->hash_size, buffer, buffer_len);
	if (retVal == 0) {
		printf("Unable to generate hash\n");
		return 0;
	}
	printf("added %s %s\n", buffer, argv[2]);

	if (directory_node != NULL)
		ipfs_node_free(directory_node);
	if (fs_repo != NULL)
		ipfs_repo_fsrepo_free(fs_repo);

	return retVal;
}