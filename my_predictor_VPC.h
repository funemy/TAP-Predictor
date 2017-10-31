// my_predictor.h
// This file contains a sample my_predictor class.
// It has a simple 32,768-entry gshare with a history length of 15 and a
// simple direct-mapped branch target buffer for indirect branch prediction.

#include <vector>
#include <map>

class my_update : public branch_update {
public:
    int index;
    int output;
    int pred_iter;
};

class my_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	64
#define TABLE_BITS	15
#define MAX_ITER 64
#define PERCEPTRON_NUM 10000
    my_update u;
    branch_info bi;
    unsigned int history;
    std::vector<char> history_vec;
    std::vector<char> vhistory_vec;
    std::vector<unsigned int> iter_tb;
    std::map<unsigned int, unsigned int> btb;
    std::map<unsigned int, int[MAX_ITER]> replace_bits;
    int w[PERCEPTRON_NUM][HISTORY_LENGTH+1];
    double threshold = 1.93 * HISTORY_LENGTH + 14;

    my_predictor () : history(0), history_vec(HISTORY_LENGTH, false){
        memset(w, 0, sizeof(w));
        srand(12450);
        for (int i =0; i < MAX_ITER; i++) {
            iter_tb.push_back(rand());
        }
    }

    unsigned int hash (unsigned int vpca, int iter) {
        return vpca ^ iter_tb.at(iter-1);
    }

    unsigned int hash_robin (unsigned int vpca) {
        vpca = (vpca+0x7ed55d16) + (vpca<<12);
        vpca = (vpca^0xc761c23c) ^ (vpca>>19);
        vpca = (vpca+0x165667b1) + (vpca<<5);
        vpca = (vpca+0xd3a2646c) ^ (vpca<<9);
        vpca = (vpca+0xfd7046c5) + (vpca<<3);
        vpca = (vpca^0xb55a4f09) ^ (vpca>>16);
        return vpca;
    }

    void vec_left_shift (std::vector<char> &vec) {
//      TODO: may cause bug
        vec.erase(vec.begin()+0);
        vec.push_back(0);
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

    unsigned int access_BTB (unsigned int vpca) {
        unsigned int t;
        try {
            t = btb.at(vpca);
        } catch (std::out_of_range &e) {
            t = NULL;
        }
        return t;
    }

    void update_replacement (unsigned int pc, int iter) {
        int *bits_row;
        bits_row = replace_bits[pc];
        bits_row[iter-1] += 1;
    }

    int get_replacement (unsigned int pc) {
        int *bit_row = replace_bits[pc];
        int index = 0;
        for (int i = 0; i < MAX_ITER; i++) {
//            TODO
//            if (pc == 134860548) {
//                printf("%d ",bit_row[i]);
//            }
            if (bit_row[i] < bit_row[index]) {
                index = i;
            }
        }
//        TODO
//        if (pc == 134860548) {
//            printf("\n");
//        }
        return index + 1;
    }

    bool conditional_predict (unsigned int vpca) {
		int i,j,output;
		i = vpca % PERCEPTRON_NUM;
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

    void conditional_update (unsigned int vpca, bool taken) {
        int j;
        int i = vpca % PERCEPTRON_NUM;
        int *wrow = w[i];
        if (taken) {
            wrow[0] += 1;
        } else {
            wrow[0] -= 1;
        }
        for (j=1; j<=HISTORY_LENGTH; j++) {
            if (taken == vhistory_vec[j-1]) {
                wrow[j] += 1;
            } else {
                wrow[j] -= 1;
            }
        }
    }

    void train (bool prediction, bool taken) {
        int j;
        int *wrow = w[u.index];
//        if ( (prediction!=taken) || (abs(u.output)<threshold) ) {
        if ( (prediction!=taken)) {
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

    unsigned int vpc_predict(unsigned int pc) {
        int iter = 1;
        bool done = false;
        unsigned int vpca = pc;
        unsigned int pred_target;
        vhistory_vec = history_vec;
        bool pred_dir;
        while (!done) {
            pred_target = access_BTB(vpca);
            pred_dir = conditional_predict(vpca);
            if (pred_target && pred_dir) {
                done = true;
            } else if (!pred_target || iter >= MAX_ITER) {
//              TODO: STALL = TRUE?
                done = true;
            }
            vpca = hash(pc, iter);
            iter += 1;
            vec_left_shift(vhistory_vec);
        }
        u.pred_iter = iter-1;
        if (pc == 134860548) {
//            printf("%u\n", btb[pc]);
        }
        return pred_target;
    }

    void vpc_update_correct (int pred_iter, unsigned int pc) {
        int iter = 1;
        unsigned int vpca = pc;
        vhistory_vec = history_vec;
        while (iter <= pred_iter) {
            if (iter == pred_iter) {
                conditional_update(vpca, true);
                update_replacement(pc, iter);
            } else {
                conditional_update(vpca, false);
            }
            vpca = hash(pc, iter);
            vec_left_shift(vhistory_vec);
            iter += 1;
        }
    }

    void vpc_update_wrong (unsigned int pc, unsigned int target) {
        int iter = 1;
        unsigned int vpca = pc;
        vhistory_vec = history_vec;
        bool found_correct_target = false;
        unsigned int pred_target;
        while ((iter <= MAX_ITER) && !found_correct_target) {
            pred_target = access_BTB(vpca);
//            printf("target: %u\n", target);
//            printf("pred: %u\n", pred_target);
            if (pred_target == target) {
                conditional_update(vpca, true);
                update_replacement(pc, iter);
                found_correct_target = true;
            } else if (pred_target) {
                conditional_update(vpca, false);
            }
            vpca = hash(pc, iter);
            vec_left_shift(vhistory_vec);
            iter += 1;
        }
        if (!found_correct_target) {
            iter = get_replacement(pc);
            if (iter == 1) {
                vpca = pc;
            } else {
                vpca = hash(pc, iter-1);
            }
            vhistory_vec = history_vec;
            for (int i = 0 ; i<(iter-1); i++) {
                vec_left_shift(vhistory_vec);
            }
            btb[vpca] = target;
//            if (pc == 134860548) {
//                printf("pc: %u\n", pc );
//                printf("add: %u\n", btb[vpca]);
//                printf("vpca: %u\n",vpca);
//                printf("iter: %d\n", iter);
//            }
            update_replacement(pc, iter);
            conditional_update(vpca, true);
        }
    }


    branch_update *predict (branch_info & b) {
        bi = b;
        if (b.br_flags & BR_CONDITIONAL) {
            bool direction = conditional_predict(b.address);
            u.direction_prediction(direction);
        } else {
            u.direction_prediction (true);
        }
        if (b.br_flags & BR_INDIRECT) {
            unsigned int target = vpc_predict(b.address);
            u.target_prediction (target);
        }
        return &u;
    }

    void update (branch_update *u, bool taken, unsigned int target) {
        if (bi.br_flags & BR_CONDITIONAL) {
            train(u->direction_prediction(), taken);
            vec_push(history_vec, taken);
        }
        if (bi.br_flags & BR_INDIRECT) {
            if (u->target_prediction() == target) {
                vpc_update_correct(((my_update*)u)->pred_iter, bi.address);
            } else {
                vpc_update_wrong(bi.address, target);
            }
        }
    }
};
