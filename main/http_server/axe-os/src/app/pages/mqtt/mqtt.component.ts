import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { tap, catchError, of, switchMap } from 'rxjs';
import { LoadingService } from '../../services/loading.service';
import { SystemService } from '../../services/system.service';
import { NbToastrService } from '@nebular/theme';
import { TranslateService } from '@ngx-translate/core';
import { OtpAuthService, EnsureOtpResult } from '../../services/otp-auth.service';

@Component({
  selector: 'app-mqtt',
  templateUrl: './mqtt.component.html',
  styleUrls: ['./mqtt.component.scss']
})
export class MqttComponent implements OnInit {

  public form!: FormGroup;
  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastrService: NbToastrService,
    private loadingService: LoadingService,
    private translate: TranslateService,
    private otpAuth: OtpAuthService,
  ) { }

  ngOnInit(): void {
    this.systemService.getMqttInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.form = this.fb.group({
          mqttEnable:      [info.mqttEnable == 1],
          mqttURI:         [info.mqttURI, [
            Validators.required,
            Validators.pattern(/^mqtt:\/\/.+/), // tls disabled in this build
          ]],
          mqttUser:        [info.mqttUser],
          mqttPass:        ['password'],
          mqttTopicPrefix: [info.mqttTopicPrefix, [Validators.required, Validators.pattern(/^[A-Za-z0-9_\-\/]+$/)]],
          mqttInterval:    [info.mqttInterval, [Validators.required, Validators.min(1), Validators.max(3600)]],
          mqttDiscovery:   [info.mqttDiscovery == 1],
        });
      });
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    const payload: any = {
      mqttEnable:      !!form.mqttEnable,
      mqttURI:         form.mqttURI,
      mqttUser:        form.mqttUser ?? '',
      mqttTopicPrefix: form.mqttTopicPrefix,
      mqttInterval:    form.mqttInterval,
      mqttDiscovery:   !!form.mqttDiscovery,
    };
    if (form.mqttPass !== 'password') {
      payload.mqttPass = form.mqttPass;
    }

    this.otpAuth.ensureOtp$(
      this.uri,
      this.translate.instant('SECURITY.OTP_TITLE'),
      this.translate.instant('SECURITY.OTP_HINT')
    )
      .pipe(
        switchMap(({ totp }: EnsureOtpResult) =>
          this.systemService.updateMqtt(this.uri, payload, totp)
            .pipe(this.loadingService.lockUIUntilComplete())
        )
      )
      .subscribe({
        next: () => {
          this.toastrService.success(this.translate.instant('MQTT.SETTINGS_SAVED'), this.translate.instant('COMMON.SUCCESS'));
        },
        error: (err: HttpErrorResponse) => {
          this.toastrService.danger(this.translate.instant('MQTT.SETTINGS_SAVE_FAILED'), `${this.translate.instant('COMMON.ERROR')}. ${err.message}`);
        }
      });
  }

  public restart() {
    this.otpAuth.ensureOtp$(
      this.uri,
      this.translate.instant('SECURITY.OTP_TITLE'),
      this.translate.instant('SECURITY.OTP_HINT'),
      { disableOtp: true },
    )
      .pipe(
        switchMap(({ totp }: EnsureOtpResult) =>
          this.systemService.restart('', totp).pipe(
            tap(() => {}),
            this.loadingService.lockUIUntilComplete()
          )
        ),
        catchError((err: HttpErrorResponse) => {
          this.toastrService.danger(this.translate.instant('SYSTEM.RESTART_FAILED'), this.translate.instant('COMMON.ERROR'));
          return of(null);
        })
      )
      .subscribe(res => {
        if (res !== null) {
          this.toastrService.success(this.translate.instant('SYSTEM.RESTART_SUCCESS'), this.translate.instant('COMMON.SUCCESS'));
        }
      });
  }
}
