// my_predictor.h
// This file contains a sample my_predictor class.
// It has a simple 32,768-entry gshare with a history length of 15 and a
// simple direct-mapped branch target buffer for indirect branch prediction.

#include <vector>

class my_update : public branch_update {
public:
    int index;
    int output;
};

class my_predictor : public branch_predictor {
#define HISTORY_LENGTH	64
#define TABLE_BITS	15
#define PERCPETRON_NUM 64
    const double threshold = 1.93 * HISTORY_LENGTH + 14;
//	const double threshold = 1000;
public:
	my_update u;
	branch_info bi;
	unsigned long long int history;
	std::vector<char> history_vec;
	unsigned int targets[1<<TABLE_BITS];
	int w[PERCPETRON_NUM][HISTORY_LENGTH+1];

	my_predictor (void) : history(0), history_vec(HISTORY_LENGTH, false){
		memset (targets, 0, sizeof (targets));
		memset(w, 0, sizeof(w));
	}

	void vec_unshift (std::vector<char> &vec) {
		vec.erase(vec.begin()+0);
	}

	void vec_push (std::vector<char> &vec, char value) {
		if (vec.size() >= HISTORY_LENGTH) {
			vec_unshift(vec);
		}
        vec.push_back(value);
	}


	bool perceptron_predict(unsigned int pc) {
		int i,j,output;
		i = pc % PERCPETRON_NUM;
		int *wrow = w[i];
		output = 0;
		for (j = 0; j <= HISTORY_LENGTH; j++) {
			if (j == 0) {
				output += wrow[j];
			} else {
                if (history_vec[j]) {
					output += wrow[j];
				} else {
					output -= wrow[j];
				}
			}
		}
        u.index = i;
		u.output = output;
		return output >= 0;
	}

	void train (int i, int output, bool prediction, bool taken) {
		int j;
		int *wrow = w[i];
		if ( (prediction!=taken) || (abs(output)<threshold) ) {
            if (taken) {
				wrow[0] += 1;
			} else {
				wrow[0] -= 1;
			}
			for (j=1; j<=HISTORY_LENGTH; j++) {
				if (taken == history_vec[j]) {
					wrow[j] += 1;
				} else {
					wrow[j] -= 1;
				}
			}
		}

	}

	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
            bool direction = perceptron_predict(b.address);
            u.direction_prediction(direction);
		} else {
			u.direction_prediction (true);
		}
		if (b.br_flags & BR_INDIRECT) {
			u.target_prediction (targets[b.address & ((1<<TABLE_BITS)-1)]);
		}
		return &u;
	}

	void update (branch_update *u, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
            train(((my_update*)u)->index, ((my_update*)u)->output, u->direction_prediction(), taken);
//			history <<= 1;
//			history |= taken;
//			history &= (1<<HISTORY_LENGTH)-1;
			vec_push(history_vec, taken);
		}
		if (bi.br_flags & BR_INDIRECT) {
//			targets[bi.address & ((1<<TABLE_BITS)-1)] = target;
		}
	}

};
