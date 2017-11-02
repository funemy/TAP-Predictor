// my_predictor.h
// This file contains a my_predictor class.
// It has a perceptron-based TAP indirect branch predictor

#include <vector>
#include <map>
#include <iostream>
#include <bitset>

// a BTB-set is indexed by a branch address
// 32-way associated
// using the LFU replacement policy when the set is full
class btb_set {
#define SUB_PREDICTOR_NUM 5
public:
    unsigned int targets[1<<SUB_PREDICTOR_NUM];
    int counter[1<<SUB_PREDICTOR_NUM];
//    int mru[1<<SUB_PREDICTOR_NUM] =
//            {31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
    btb_set () {
        memset(targets, 0 , sizeof(targets));
        memset(counter, 0 , sizeof(counter));
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
//History length for each sub-predictor
#define HISTORY_LENGTH 64
//Number of perceptrons
#define PERCPETRON_NUM 10000
//The total weight vector length
#define LONG_HISTORY_LENGTH ((HISTORY_LENGTH+1)*SUB_PREDICTOR_NUM)
//  threshold for perceptron training
    const double threshold = 1.93 * HISTORY_LENGTH + 14;
public:
    my_update u;
    branch_info bi;
//  Global History Register
    std::vector<char> history_vec;
    std::vector<std::vector<char>> local_history_vec;
//	a weight matrix for sub predictor perceptrons
    int wsub[PERCPETRON_NUM][LONG_HISTORY_LENGTH];
//  Branch target buffer
//  indexed by indirect branch address
    std::map<unsigned int, btb_set> btb;

    my_predictor (void) : history_vec(LONG_HISTORY_LENGTH-1, false){
        memset(wsub, 0, sizeof(wsub));
        for (int i = 0; i < SUB_PREDICTOR_NUM; ++i) {
            local_history_vec.emplace_back(std::vector<char>(HISTORY_LENGTH, false));
        }
    }


    void vec_unshift (std::vector<char> &vec) {
        vec.erase(vec.begin()+0);
    }

//  push new history to the GHR
//  while keeping the GHR at the same length
    void vec_push (std::vector<char> &vec, char value) {
        if (vec.size() >= LONG_HISTORY_LENGTH-1) {
            vec_unshift(vec);
        }
        vec.push_back(value);
    }

//  conditional branch prediction
    bool perceptron_predict(unsigned int pc) {
        int i,j,output;
        i = pc % PERCPETRON_NUM;
        int *wrow = wsub[i];
        output = 0;
        for (j = 0; j <= LONG_HISTORY_LENGTH; j++) {
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

//  conditional update
    void train (int i, int output, bool prediction, bool taken) {
        int j;
        int *wrow = wsub[i];
        if ( (prediction!=taken) || (abs(output)<threshold) ) {
            if (taken) {
                wrow[0] += 1;
            } else {
                wrow[0] -= 1;
            }
            for (j=1; j<=LONG_HISTORY_LENGTH; j++) {
                if (taken == history_vec[j-1]) {
                    wrow[j] += 1;
                } else {
                    wrow[j] -= 1;
                }
            }
        }
    }

//  indirect branch prediction
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
                    int index = (k%w_len)-1 + w_len*(SUB_PREDICTOR_NUM-1);
                    if (history_vec[index]) {
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

//  indirect branch prediction
    void sub_predictor_train(int i, int outputs[], int tap, unsigned int prediction, unsigned int target){
        int j, k, w_len;
        int *wrow = wsub[i];
        w_len = HISTORY_LENGTH + 1;
//      condition 1:
//		prediction equals to target
//      only update the BTB
//      using LFU replacement policy
        if (prediction == target) {
            btb_set *set = &btb[bi.address];
            set->counter[tap] += 1;

//          the commented code below is LRU replacement policy
//          beaten by LFU

//            int tap_i = 0;
//            for (j=0; j<(1<<SUB_PREDICTOR_NUM); j++) {
//                if (set->mru[j] == tap) {
//                    tap_i = j;
//                    break;
//                }
//            }
//            for (k=tap_i; k>0; k--) {
//                set->mru[k] = set->mru[k-1];
//            }
//            set->mru[0] = tap;

//		condition 2:
//		prediction is not equals to target
        } else {
            btb_set *set = &btb[bi.address];
            int real_tap = 0;
//          LFU policy
            for (j = 0; j < (1 << SUB_PREDICTOR_NUM); j++) {
                if (set->targets[j] == target) {
                    real_tap = j;
                    break;
                }
                if (set->counter[j] < set->counter[real_tap]) {
                    real_tap = j;
                }
            }
            set->counter[real_tap] += 1;
            set->targets[real_tap] = target;

//          LRU policy

//            int real_tap_i=0;
//            for (j=0; j<(1<<SUB_PREDICTOR_NUM); j++) {
//                if (set->targets[j] == target) {
//                    real_tap = j;
//                    break;
//                }
//            }
//            if (set->targets[real_tap] != target) {
//                real_tap_i = (1<<SUB_PREDICTOR_NUM) - 1;
//                real_tap = set->mru[real_tap_i];
//                set->targets[real_tap] = target;
//            } else {
//                for (j=0; j<(1<<SUB_PREDICTOR_NUM); j++) {
//                    if (set->mru[j] == real_tap) {
//                        real_tap_i = j;
//                        break;
//                    }
//                }
//            }
//            for (k=real_tap_i; k>0; k--) {
//                set->mru[k] = set->mru[k-1];
//            }
//            set->mru[0] = real_tap;

//          training perceptrons
            for (j=0; j<SUB_PREDICTOR_NUM; j++) {
                bool tap_bit = tap & (1<<(SUB_PREDICTOR_NUM-j-1));
                bool taken = real_tap & (1<<(SUB_PREDICTOR_NUM-j-1));

//              the commented code below compares each output from perceptron predictor with threshold
//                if ( (tap_bit != taken) || (outputs[j]<threshold) ) {
                if ( (tap_bit != taken) ) {
                    for (k=w_len*j; k<w_len*(j+1); k++) {
                        if ( (k % w_len) == 0 ) {
                            if (taken) wrow[k] += 1; else wrow[k] -= 1;
                        } else {
                            int index = (k%w_len)-1 + w_len*(SUB_PREDICTOR_NUM-1);
                            if (taken == history_vec[index]) {
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
//            bool direction = perceptron_predict(b.address);
//            u.direction_prediction(direction);
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
//            train(((my_update*)u)->index, ((my_update*)u)->output, u->direction_prediction(), taken);
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
