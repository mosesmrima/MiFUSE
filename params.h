//
// Created by mrima on 6/3/21.
//

#ifndef MIFUSE_PARAMS_H

struct mi_state{
	FILE *logfile;
    	char *root_dir;
};

#define MI_DATA ((struct mi_state *) fuse_get_context()->private_data)
#endif //MIFUSE_PARAMS_H
