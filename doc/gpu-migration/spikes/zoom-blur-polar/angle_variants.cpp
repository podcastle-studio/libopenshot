#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cmath>
int main(){
    const int PW=274, PH=274; cv::Point2f pc(137.f,137.f); double maxR=193.747258045;
    const double Kangle=2*CV_PI/PH;  const float Kanglef=(float)Kangle;
    const double Kmag=maxR/PW;
    cv::Mat coord(PH,PW,CV_32FC2);
    for(int y=0;y<PH;++y)for(int x=0;x<PW;++x) coord.at<cv::Vec2f>(y,x)=cv::Vec2f((float)x,(float)y);
    cv::Mat inv; cv::linearPolar(coord,inv,pc,maxR,cv::WARP_INVERSE_MAP);

    // cartToPolar over the whole grid, exactly as warpPolar does it (row at a time)
    long bad[5]={0,0,0,0,0};
    for(int y=0;y<PH;++y){
        cv::Mat bx(1,PW,CV_32F), by(1,PW,CV_32F);
        for(int x=0;x<PW;++x){ bx.at<float>(0,x)=(float)x-pc.x; by.at<float>(0,x)=(float)y-pc.y; }
        cv::Mat mag, ang; cv::cartToPolar(bx,by,mag,ang,false);
        for(int x=0;x<PW;++x){
            double dx=x-(double)pc.x, dy=y-(double)pc.y;
            if(cvRound(std::sqrt(dx*dx+dy*dy)/Kmag)>=PW) continue;
            float a=ang.at<float>(0,x);
            double cand[5];
            cand[0]=cvRound((double)a/Kangle);
            cand[1]=cvRound((double)(a/Kanglef));
            cand[2]=cvRound((double)(a*(1.0f/Kanglef)));
            cand[3]=cvRound((double)a*(1.0/Kangle));
            cand[4]=cvRound((double)(float)((double)a/Kangle));
            float g=inv.at<cv::Vec2f>(y,x)[1];
            for(int k=0;k<5;++k){ double c=cand[k]; if(c>=PH)c-=PH; double d=std::abs(g-c); if(d>PH/2)d=std::abs(d-PH); if(d>1e-3) bad[k]++; }
        }
    }
    const char* names[5]={"cartToPolar /Kangle(double)","cartToPolar /Kangle(float)","cartToPolar *(1/Kangle)(float)","cartToPolar *(1/Kangle)(double)","cartToPolar /Kangle then to float"};
    for(int k=0;k<5;++k) printf("%-38s %ld bad\n", names[k], bad[k]);
    return 0;
}
