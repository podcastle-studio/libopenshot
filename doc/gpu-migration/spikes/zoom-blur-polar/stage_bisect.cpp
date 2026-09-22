// Stage-by-stage: where does my closed form diverge from cv::linearPolar?
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <random>
#include <cmath>

int main(){
    const int W=256,H=256; const int authored=40; const double cx01=0.5, cy01=0.5;
    cv::Mat src(H,W,CV_8UC4); std::mt19937 rng(20260922u);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){int a=rng()&255;
        src.at<cv::Vec4b>(y,x)=cv::Vec4b((uchar)std::min<int>(rng()&255,a),(uchar)std::min<int>(rng()&255,a),(uchar)std::min<int>(rng()&255,a),(uchar)a);}

    int strength=std::max(1,(int)std::lround(authored*(W/1280.0))); if(strength%2==0) strength+=1;
    const int pad=strength;
    cv::Mat padded; cv::copyMakeBorder(src,padded,pad,pad,pad,pad,cv::BORDER_REFLECT);
    const int PW=padded.cols, PH=padded.rows;
    cv::Point2f pc((float)(cx01*W)+pad,(float)(cy01*H)+pad);
    double maxR=0;
    std::vector<cv::Point2f> corners={{0,0},{(float)(PW-1),0},{0,(float)(PH-1)},{(float)(PW-1),(float)(PH-1)}};
    for(auto&c:corners) maxR=std::max(maxR,cv::norm(c-pc));
    printf("strength=%d pad=%d PW=%d PH=%d center=(%.3f,%.3f) maxR=%.9f\n",strength,pad,PW,PH,pc.x,pc.y,maxR);

    cv::Mat polar; cv::linearPolar(padded,polar,pc,maxR,cv::WARP_FILL_OUTLIERS);
    printf("polar size %dx%d\n", polar.cols, polar.rows);

    // my forward
    const double Kangle=2*CV_PI/PH, Kmag=maxR/PW;
    long bad=0; int worst=0;
    cv::Mat mine(PH,PW,CV_8UC4, cv::Scalar(0,0,0,0));
    for(int phi=0;phi<PH;++phi){ double a=Kangle*phi, cp=std::cos(a), sp=std::sin(a);
      for(int rho=0;rho<PW;++rho){
        int sx=cvRound(pc.x+Kmag*rho*cp), sy=cvRound(pc.y+Kmag*rho*sp);
        cv::Vec4b v(0,0,0,0);
        if((unsigned)sx<(unsigned)PW && (unsigned)sy<(unsigned)PH) v=padded.at<cv::Vec4b>(sy,sx);
        mine.at<cv::Vec4b>(phi,rho)=v;
        for(int c=0;c<4;++c){int e=std::abs((int)v[c]-(int)polar.at<cv::Vec4b>(phi,rho)[c]); if(e){bad++;worst=std::max(worst,e);} }
      }}
    printf("FORWARD: %ld of %d channels differ, worst %d\n", bad, PW*PH*4, worst);

    // blur both
    cv::Mat pb=polar.clone(), mb=mine.clone();
    cv::blur(pb,pb,cv::Size(strength,1)); cv::blur(mb,mb,cv::Size(strength,1));

    // inverse via opencv from the true blurred polar
    cv::Mat back=padded.clone(); cv::linearPolar(pb,back,pc,maxR,cv::WARP_INVERSE_MAP);
    // my inverse from the same true blurred polar
    long bad2=0; int worst2=0; long outside=0;
    for(int y=0;y<PH;++y)for(int x=0;x<PW;++x){
        double dx=x-(double)pc.x, dy=y-(double)pc.y;
        int rho=cvRound(std::sqrt(dx*dx+dy*dy)/Kmag);
        double ang=std::atan2(dy,dx); if(ang<0) ang+=2*CV_PI;
        int phi=cvRound(ang/Kangle); if(phi>=PH) phi-=PH;
        cv::Vec4b v;
        if((unsigned)rho<(unsigned)PW) v=pb.at<cv::Vec4b>(phi,rho);
        else { v=padded.at<cv::Vec4b>(y,x); outside++; }
        for(int c=0;c<4;++c){int e=std::abs((int)v[c]-(int)back.at<cv::Vec4b>(y,x)[c]); if(e){bad2++;worst2=std::max(worst2,e);} }
    }
    printf("INVERSE: %ld of %d channels differ, worst %d (%ld pixels outside)\n", bad2, PW*PH*4, worst2, outside);
    return 0;
}
