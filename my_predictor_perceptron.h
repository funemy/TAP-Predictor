// my_predictor.h
// This file contains a sample my_predictor class.
// It has a simple 32,768-entry gshare with a history length of 15 and a
// simple direct-mapped branch target buffer for indirect branch prediction.

#include <vector>
#include <map>
#include <iostream>
#include <bitset>

// a BTB-block is indexed by a branch address
// it contains at most 16 branch targets
// using the LRU replacement policy when the block is full
class btb_block {
#define SUB_PREDICTOR_NUM 5
public:
    unsigned int targets[1<<SUB_PREDICTOR_NUM];
    int mru[1<<SUB_PREDICTOR_NUM] =
            {31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
    btb_block () {
        memset(targets, 0 , sizeof(targets));
    }
};

class my_update : public branch_update {
public:
    int index;
    int tap;
    int output;
    int outputs[SUB_PREDICTOR_NUM] = {0,0,0,0,0};
};

class my_predictor : public branch_predictor {
#define HISTORY_LENGTH	22
#define PERCPETRON_NUM 10000
    const double threshold = 1.93 * HISTORY_LENGTH + 14;
public:
    my_update u;
    branch_info bi;
    unsigned long long int history;
    std::vector<char> history_vec;
//	a weight matrix for percpetrons
    int w[PERCPETRON_NUM][HISTORY_LENGTH+1];
//	a weight matrix for sub predictor perceptrons
//	TODO: could have bugs for initialization
    int wsub[PERCPETRON_NUM][(HISTORY_LENGTH+1)*SUB_PREDICTOR_NUM];
    std::map<unsigned int, btb_block> btb;

    my_predictor (void) : history(0), history_vec(HISTORY_LENGTH, false){
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
                if (history_vec[j-1]) {
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
                if (taken == history_vec[j-1]) {
                    wrow[j] += 1;
                } else {
                    wrow[j] -= 1;
                }
            }
        }
    }

    unsigned int indirect_prediction(unsigned int pc) {
        int i, j, k, w_len, output, tap;
        i = pc % PERCPETRON_NUM;
        w_len = HISTORY_LENGTH + 1;
        int *wrow = wsub[i];
        tap = 0;
        for (j=0; j<SUB_PREDICTOR_NUM; j++) {
            output = 0;
            for (k=w_len*j; k<w_len*(j+1); k++) {
                if ( (k % w_len) == 0 ) {
                    output += wrow[k];
                } else {
                    if (history_vec[(k % w_len) - 1]) {
                        output += wrow[k];
                    } else {
                        output -= wrow[k];
                    }
                }
            }
            if (output >= 0) {
                tap <<= 1;
                tap |= 1;
            } else {
                tap <<= 1;
            }
            u.outputs[j] = output;
        }
        u.index = i;
        u.tap = tap;
        return btb[pc].targets[tap];
    }

    void sub_predictor_train(int i, int outputs[], int tap, unsigned int prediction, unsigned int target){
        int j, k, w_len;
        int *wrow = wsub[i];
        w_len = HISTORY_LENGTH + 1;
//      condition 1:
//		prediction equals to target, but output is smaller than threshold
        if (prediction == target) {
            btb_block *block = &btb[bi.address];
            int tap_i = 0;
            for (j=0; j<(1<<SUB_PREDICTOR_NUM); j++) {
                if (block->mru[j] == tap) {
                    tap_i = j;
                    break;
                }
            }
            for (k=tap_i; k>0; k--) {
                block->mru[k] = block->mru[k-1];
            }
            block->mru[0] = tap;
//		condition 2:
//		prediction is not equals to target
        } else {
            int real_tap = 0; // TODO: may cause bug, switch to 0;
            int real_tap_i=0;
            btb_block *block = &btb[bi.address];
            bool mistake_flag = false; // TODO: for debugging
            for (j=0; j<(1<<SUB_PREDICTOR_NUM); j++) {
                if (block->targets[j] == target) {
                    real_tap = j;
                    break;
                }
            }
            if (block->targets[real_tap] != target) {
                real_tap_i = (1<<SUB_PREDICTOR_NUM) - 1;
                real_tap = block->mru[real_tap_i];
                block->targets[real_tap] = target;
            } else {
                for (j=0; j<(1<<SUB_PREDICTOR_NUM); j++) {
                    if (block->mru[j] == real_tap) {
                        real_tap_i = j;
                        break;
                    }
                }
            }
            for (k=real_tap_i; k>0; k--) {
                block->mru[k] = block->mru[k-1];
            }
            block->mru[0] = real_tap;
            for (j=0; j<SUB_PREDICTOR_NUM; j++) {
                bool tap_bit = tap >> (SUB_PREDICTOR_NUM - j -1);
                bool taken = real_tap >> (SUB_PREDICTOR_NUM - j -1);
                if ( (tap_bit != taken) || (outputs[j] < threshold) ) {
                    for (k=w_len*j; k<w_len*(j+1); k++) {
                        if ( (k % w_len) == 0 ) {
                            if (taken) wrow[k] += 1; else wrow[k] -= 1;
                        } else {
                            if (taken == history_vec[(k % w_len) - 1]) {
                                wrow[k] += 1;
                            } else {
                                wrow[k] -= 1;
                            }
                        }
                    }
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
            unsigned int target = indirect_prediction(b.address);
            u.target_prediction(target);
        }
        return &u;
    }

    void update (branch_update *u, bool taken, unsigned int target) {
        if (bi.br_flags & BR_CONDITIONAL) {
            train(((my_update*)u)->index, ((my_update*)u)->output, u->direction_prediction(), taken);
            vec_push(history_vec, taken);
        }
        if (bi.br_flags & BR_INDIRECT) {
            sub_predictor_train(
                    ((my_update*)u)->index,
                    ((my_update*)u)->outputs,
                    ((my_update*)u)->tap,
                    u->target_prediction(),
                    target
            );
        }
    }

};
