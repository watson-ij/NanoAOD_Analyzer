#include "JetScaleResolution.h"
#include <sstream>
#include <cmath>



JetScaleResolution::JetScaleResolution(){
}

JetScaleResolution::JetScaleResolution(const std::string& scalefilename, const std::string& parttype, const std::string& resolutionfile, const std::string& sfresolutionfile){
    InitScale(scalefilename, parttype);
    InitResolution(resolutionfile, sfresolutionfile);

    resfilename = resolutionfile;
    resfunctyfilename = sfresolutionfile;
    scaleunctyfilename = scalefilename;
    type_name = parttype;
}

double JetScaleResolution::GetScaleDelta(const double& recojetPt, const double& recojetEta){
    if(!(recojetPt > 0.0)){
        return recojetPt;
    }

    jetcorrectorparams = JetCorrectorParameters(scaleunctyfilename,type_name);

    JetCorrectionUncertainty jesUncertainty = JetCorrectionUncertainty(jetcorrectorparams);

    //Call jesuncertainty with Pt being that obtained after applying nominal jet energy resolution scale factor
    // jet_pt_nom = jer_sf_nom * recojet.Pt();
    jesUncertainty.setJetPt(recojetPt);
    jesUncertainty.setJetEta(recojetEta);

    double delta = jesUncertainty.getUncertainty(true);    

    return delta;
}

double JetScaleResolution::GetSmearValsPtSF(const TLorentzVector& recojet, const TLorentzVector& genjet, double rho, double sigmares){
    if(!(recojet.Pt() > 0.0)){
        return recojet.Pt();
    }

    jer = JME::JetResolution(resfilename);
    jerSF_and_Uncertainty = JME::JetResolutionScaleFactor(resfunctyfilename);

    auto params_sf_and_uncertainty = JME::JetParameters();
    auto params_resolution = JME::JetParameters();

    // Call params_resolution:
    params_resolution.setJetPt(recojet.Pt());
    params_resolution.setJetEta(recojet.Eta());
    params_resolution.setRho(rho);

    float jet_pt_resolution = jer.getResolution(params_resolution);

    // CV: define enums to access JER scale factors and uncertainties (cf. CondFormats/JetMETObjects/interface/JetResolutionObject.h) 
    int index_nominal = 0, index_shift_down = 1, index_shift_up = 2;

    float jet_pt_sf_and_uncertainty[3] = { };
    float smear_values[3] = { }; 
    
    for(int index = 0; index < 3; index++){
        Variation var = (Variation) index;
        params_sf_and_uncertainty.setJetEta(recojet.Eta());
        jet_pt_sf_and_uncertainty[index] = jerSF_and_Uncertainty.getScaleFactor(params_sf_and_uncertainty, var);
    }

    for(int index = 0; index < 3; index++){
        double smearFactor = 0.0;

        // Case 1: we have a "good" generator level jet matched to the reconstructed jet
        // Case 2: we don't have a generator level jet. Smear jet pT using a random Gaussian variation
        // Case 3: we cannot smear this jet, as we don't have a generator level jet and the resolution in data is better than the resolution in the simulation, 

        if(genjet != TLorentzVector(0,0,0,0)){
            double deltaPt = recojet.Pt() - genjet.Pt();
            smearFactor = 1.0 + (jet_pt_sf_and_uncertainty[index] - 1.0) * (deltaPt / recojet.Pt());
        }
        else if(jet_pt_sf_and_uncertainty[index] > 1.0){
            double sigma = jet_pt_resolution * sqrt( pow(jet_pt_sf_and_uncertainty[index], 2) - 1.0);
            smearFactor = rnd->Gaus(1.0,sigma);
        }
        else{
            smearFactor = 1.0;
        }

        if(smearFactor < 0.0) smearFactor = 0.0;

        // Check that the smeared jet energy remains positive, as the direction of the jet would change ("flip") otherwise, and this is not what we want
        if(smearFactor * recojet.Pt() < 1.0e-2){
            smearFactor = 1.0e-2;
        }

        smear_values[index] = smearFactor;
    }

    // Return the appropriate value depending on what you want:
    if(sigmares == 0.0){ // nominal value
        return smear_values[index_nominal];
    }
    else if(sigmares == -1.0){ // down value
        return smear_values[index_shift_down];
    }
    else if(sigmares == 1.0){ // up value
        return smear_values[index_shift_up];
    }

    return smear_values[index_nominal];
}

void JetScaleResolution::InitScale(const std::string& filename, const std::string& type)
{
    std::vector<TH1D*>* ErrP = &HptsP;
    std::vector<TH1D*>* ErrM = &HptsM;

    //TDirectory* dir = gDirectory;
    //gROOT->cd();
    std::fstream fs(filename.c_str(), std::fstream::in);
    if(!fs.good()){
        std::cout<<"Jet file "<<filename<<" does not exist!"<<std::endl;
        exit(2);
    }
    std::string line;
    int selected = -1;
    std::vector<double> etabins;
    while(!fs.eof())
    {
      getline(fs, line);
      if(line.size() == 0) continue;
      selected++;
      if(selected>0){
        //cout<<line<<std::endl;
        std::vector<std::string> vals = string_split(line);
        if(selected == 0)
        {
            etabins.push_back(stringtotype<double>(vals[0]));
        }
        etabins.push_back(stringtotype<double>(vals[1]));
        std::vector<double> pts;
        std::vector<double> errm;
        std::vector<double> errp;
        for(size_t p = 3; p < vals.size() ; p+=3)
        {
            if(vals[p].size() == 0) break;
            pts.push_back(stringtotype<double>(vals[p]));
            errm.push_back(stringtotype<double>(vals[p+1]));
            errp.push_back(stringtotype<double>(vals[p+2]));
            //cout << p << " " << pts.back() << " " << errm.back() << " " << errp.back() << std::endl;
        }
        std::stringstream name;
        name << "jeterror_" << type << "_" << selected-2;
        ErrM->push_back(new TH1D((name.str()+"M").c_str(), (name.str()+"M").c_str(), pts.size()-1, pts.data()));
        ErrP->push_back(new TH1D((name.str()+"P").c_str(), (name.str()+"P").c_str(), pts.size()-1, pts.data()));
        for(size_t b = 0 ; b < pts.size()-1 ; ++b)
        {
            ErrM->back()->SetBinContent(b+1, errm[b]);
            ErrP->back()->SetBinContent(b+1, errp[b]);
        }
      }
    }
    Heta = new TH1D(("etabins_"+type).c_str(), ("etabins_"+type).c_str(), etabins.size()-1, etabins.data());
    if(etabins.size() == 0)
    {
        std::cerr << "ERROR - Jetscaler.InitScale: Could not find " << type << " in file " << filename << std::endl;
    }
    fs.close();
    //dir->cd();
}


void JetScaleResolution::InitResolution(const std::string& resolutionfile, const std::string& sffile)
{
    std::fstream fs(resolutionfile.c_str(), std::fstream::in);
    std::string line;
    while(!fs.eof())
    {
        getline(fs, line);
        if(line.size() == 0 || line[0] == '{') continue;
        std::vector<std::string> vals = string_split(line, {" ", "\t"});
        //for(std::string s:vals) std::cout << "|" << s << "|";
        //cout << std::endl;
        //cout << vals.size() << " " << vals[0] << " " << vals[1] << std::endl;
        if(vals.size() != 11) continue;

        Bin eta(stringtotype<double>(vals[0]), stringtotype<double>(vals[1]));
        Bin rho(stringtotype<double>(vals[2]), stringtotype<double>(vals[3]));
        //cout << stringtotype<double>(vals[0]) << " " << stringtotype<double>(vals[1]) << " " <<stringtotype<double>(vals[2]) << " " <<stringtotype<double>(vals[3]) << std::endl;
        resinfo[eta][rho] = {stringtotype<double>(vals[7]), stringtotype<double>(vals[8]), stringtotype<double>(vals[9]), stringtotype<double>(vals[10])};
    }
    fs.close();
    //cout << "finished "<< resinfo.size() << std::endl;
    std::fstream fsc(sffile.c_str(), std::fstream::in);
    while(!fsc.eof())
    {
        getline(fsc, line);
        if(line.size() == 0 || line[0] == '{') continue;
        std::vector<std::string> vals = string_split(line, {" ", "\t"});
        if(vals.size() != 6) continue;

        Bin eta(stringtotype<double>(vals[0]), stringtotype<double>(vals[1]));
        ressf[eta] = {stringtotype<double>(vals[3]), stringtotype<double>(vals[4]), stringtotype<double>(vals[5])};
    }
    fsc.close();
}



double JetScaleResolution::GetRes(const TLorentzVector& jet,const TLorentzVector& genjet, double rho, double sigmares)
{
    double rescor = 1.;
    if(rho > 44) {rho = 44;}
    if(abs(jet.Eta()) >= 4.7) {return 1.;}
    //cout << jet.Eta() << " " << rho << std::endl;
    const std::vector<double>& par = resinfo[jet.Eta()][rho];
    double x = jet.Pt();
    double resolution = sqrt(par[0]*abs(par[0])/(x*x)+par[1]*par[1]*pow(x,par[3])+par[2]*par[2]);
    //cout << jet.Eta() << " " << rho << " - " << resolution << ": " << par[0] << " " << par[1]<< " " << par[2] << std::endl;

    const std::vector<double>& sfs = ressf[jet.Eta()];
    if(sfs.size()==0){
      return(1.);
    }
    double s = sfs[0];
    //cout << sfs[0] << " " << sfs[1] << " " << sfs[2] << std::endl;
    if(sigmares <= 0) {s = sfs[0] + sigmares*(sfs[0]-sfs[1]);}
    if(sigmares > 0) {s = sfs[0] + sigmares*(sfs[2]-sfs[0]);}


    if(genjet != TLorentzVector(0,0,0,0) )
    {
        rescor +=  (s-1)*(jet.Pt()-genjet.Pt())/jet.Pt();
    }
    else
    {
        rescor += gRandom->Gaus(0., resolution*sqrt(s*s-1.));
    }
    return(std::max({0., rescor}));
}

double JetScaleResolution::GetScale(const TLorentzVector& jet, bool isBjet, double sigmascale)
{
    double sf = 1.;
    if(abs(jet.Eta()) >= 5.4) {return 1.;}

//  //cout << jet.Eta() << " " << rho << std::endl;
//  //cout << (resinfo.find(jet.Eta()) - resinfo.begin()) << std::endl;
//  const std::vector<double>& par = resinfo[jet.Eta()][rho];
//  double x = jet.Pt();
//  double resolution = sqrt(par[0]*abs(par[0])/(x*x)+par[1]*par[1]*pow(x,par[3])+par[2]*par[2]);
//  //cout << jet.Eta() << " " << rho << " - " << resolution << ": " << par[0] << " " << par[1]<< " " << par[2]<< " " << par[3] << std::endl;
//  const std::vector<double>& sfs = ressf[jet.Eta()];
//  double s = sfs[0];
//  //cout << sfs[0] << " " << sfs[1] << " " << sfs[2] << std::endl;
//  if(sigmares <= 0) {s = sfs[0] + sigmares*(sfs[0]-sfs[1]);}
//  if(sigmares > 0) {s = sfs[0] + sigmares*(sfs[2]-sfs[0]);}
//  sf = gRandom->Gaus(1., resolution*sqrt(s*s-1.));

    //MC specific correction
    //if(hlB != nullptr)
    //{
        //double mccorr = 1.;
        //if(isBjet)
        //{
            //if(abs(jet.Eta()) < 1.5)
            //{
                //mccorr = hbB->Eval(jet.Pt());
            //}
            //else
            //{
                //mccorr = hbE->Eval(jet.Pt());
            //}
        //}
        //else
        //{
            //if(abs(jet.Eta()) < 1.5)
            //{
                //mccorr = hlB->Eval(jet.Pt());
            //}
            //else
            //{
                //mccorr = hlE->Eval(jet.Pt());
            //}
        //}
        //sf += mccorr;
    //}

    int etabin = Heta->FindFixBin(jet.Eta());
    int ptbin = HptsP[etabin]->FindFixBin(jet.Pt());
    if(sigmascale >= 0)
    {
        if(isBjet) return(sf + sigmascale*sqrt(pow(HptsP[etabin]->GetBinContent(ptbin), 2) + pow(HptsPb[etabin]->GetBinContent(ptbin), 2)));
        return(sf + sigmascale*HptsP[etabin]->GetBinContent(ptbin));
    }
    else
    {
        if(isBjet) return(sf + sigmascale*sqrt(pow(HptsM[etabin]->GetBinContent(ptbin), 2) + pow(HptsMb[etabin]->GetBinContent(ptbin), 2)));
        return(sf + sigmascale*(HptsM[etabin]->GetBinContent(ptbin)));
    }
}

std::vector<std::string> string_split(const std::string& in, const std::vector<std::string> splits)
{
    std::vector<std::pair<size_t, size_t> > positions;
    positions.push_back(std::pair<size_t, size_t>(0, 0));
    for(size_t s = 0 ; s < splits.size() ; ++s)
    {
        size_t lastpos = 0;
        while(lastpos < in.size())
        {
            lastpos = in.find(splits[s], lastpos);
            if(lastpos == std::string::npos)
            {
                break;
            }
            else
            {
                positions.push_back(std::pair<size_t, size_t>(lastpos, splits[s].size()));
                //lastpos += splits[s].size()+1;
                lastpos += splits[s].size();
            }
        }

    }
    positions.push_back(std::pair<size_t, size_t>(in.size(), 0));
    sort(positions.begin(), positions.end(), [](const std::pair<size_t, size_t>& A, const std::pair<size_t, size_t>& B){return A.first < B.first;});
    std::vector<std::string> result;
    for(size_t p = 0 ; p < positions.size()-1 ; ++p)
    {
        size_t begin = positions[p].first + positions[p].second;
        size_t end = positions[p+1].first;
        if(end != begin)result.push_back(in.substr(begin, end-begin));
    }
    return result;
}


