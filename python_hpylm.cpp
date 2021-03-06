#include <iostream>
#include <string>
#include <unordered_map> 
#include <boost/python.hpp>
#include "core/c_printf.h"
#include "core/node.h"
#include "core/hpylm.h"
#include "core/vocab.h"

using namespace boost;

template<class T>
python::list list_from_vector(vector<T> &vec){  
	 python::list list;
	 typename vector<T>::const_iterator it;

	 for(it = vec.begin(); it != vec.end(); ++it)   {
		  list.append(*it);
	 }
	 return list;
}

// Pythonラッパー
class PyHPYLM{
private:
	HPYLM* hpylm;

public:
	PyHPYLM(int ngram){
		hpylm = new HPYLM(ngram);
		c_printf("[n]%d-gram %s", ngram, "HPYLMを初期化しています ...\n");
	}
	// 基底分布 i.e. 単語（文字）0-gram確率
	// 1 / 単語数（文字数）でよい
	void set_g0(double g0){
		hpylm->_g0 = g0;
		printf("G0 <- %lf\n", g0);
	}
	bool save(string filename){
		printf("HPYLMを保存しています ...\n");
		return hpylm->save(filename);
	}
	bool load(string filename){
		printf("HPYLMを読み込んでいます ...\n");
		return hpylm->load(filename);
	}
	void perform_gibbs_sampling(python::list &sentence, bool first_addition = false){
		std::vector<id> token_ids;
		int len = python::len(sentence);
		for(int i = 0;i < len;i++) {
			token_ids.push_back(python::extract<id>(sentence[i]));
		}
		for(int token_t_index = hpylm->ngram() - 1;token_t_index < token_ids.size();token_t_index++){
			if(first_addition == false){
				hpylm->remove_customer_at_timestep(token_ids, token_t_index);
			}
			hpylm->add_customer_at_timestep(token_ids, token_t_index);
		}
	}
	int get_max_depth(){
		return hpylm->get_max_depth();
	}
	int get_num_nodes(){
		return hpylm->get_num_nodes();
	}
	int get_num_customers(){
		return hpylm->get_num_customers();
	}
	python::list count_tokens_of_each_depth(){
		unordered_map<int, int> counts_by_depth;
		hpylm->count_tokens_of_each_depth(counts_by_depth);

		// ソート
		std::map<int, int> sorted_counts_by_depth(counts_by_depth.begin(), counts_by_depth.end());

		std::vector<int> counts;
		for(auto it = sorted_counts_by_depth.begin(); it != sorted_counts_by_depth.end(); ++it){
			counts.push_back(it->second);
		}
		return list_from_vector(counts);
	}
	python::list get_discount_parameters(){
		return list_from_vector(hpylm->_d_m);
	}
	python::list get_strength_parameters(){
		return list_from_vector(hpylm->_theta_m);
	}
	void sample_hyperparameters(){
		hpylm->sample_hyperparams();
	}
	id sample_next_token(python::list &_context_token_ids, id eos_id){
		std::vector<id> context_token_ids;
		int len = python::len(_context_token_ids);
		for(int i = 0; i<len; i++) {
			context_token_ids.push_back(python::extract<id>(_context_token_ids[i]));
		}
		return hpylm->sample_next_token(context_token_ids, eos_id);
	}
	double log2_Pw(python::list &sentence){
		std::vector<id> token_ids;
		int len = python::len(sentence);
		for(int i = 0; i<len; i++) {
			token_ids.push_back(python::extract<id>(sentence[i]));
		}
		return hpylm->log2_Pw(token_ids);
	}
};

BOOST_PYTHON_MODULE(hpylm){
	python::class_<PyHPYLM>("hpylm", python::init<int>())
	.def("set_g0", &PyHPYLM::set_g0)
	.def("perform_gibbs_sampling", &PyHPYLM::perform_gibbs_sampling)
	.def("get_max_depth", &PyHPYLM::get_max_depth)
	.def("get_num_nodes", &PyHPYLM::get_num_nodes)
	.def("get_num_customers", &PyHPYLM::get_num_customers)
	.def("get_discount_parameters", &PyHPYLM::get_discount_parameters)
	.def("get_strength_parameters", &PyHPYLM::get_strength_parameters)
	.def("sample_hyperparameters", &PyHPYLM::sample_hyperparameters)
	.def("log2_Pw", &PyHPYLM::log2_Pw)
	.def("sample_next_token", &PyHPYLM::sample_next_token)
	.def("count_tokens_of_each_depth", &PyHPYLM::count_tokens_of_each_depth)
	.def("save", &PyHPYLM::save)
	.def("load", &PyHPYLM::load);
}