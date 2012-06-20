uniform sampler2DShadow shadowMap ;

// This define the value to move one pixel left or right
uniform vec2 pixelOffset;// = vec2(1.0 / 1024, 1.0 / 1024);
uniform sampler2D tex0;
varying vec2 texCoord;

varying vec4 ShadowCoord;


varying vec4 diffuse,ambientGlobal, ambient;
varying vec3 normal,lightDir,halfVector;
varying float dist;

vec4 pointlight()
{
	vec3 n,halfV,viewV,ldir;
	float NdotL,NdotHV;
	vec4 color = ambientGlobal;
	float att;
	
	/* a fragment shader can't write a verying variable, hence we need
	a new variable to store the normalized interpolated normal */
//	n = normalize(normal);
	n = normal;
	
	/* compute the dot product between normal and ldir */
//	NdotL = max(dot(n,normalize(lightDir)),0.0);
//	NdotL = max(dot(n, lightDir), 0.0);
	NdotL = dot(n, lightDir);

	if (NdotL > 0.0) {
		att = 1.0 / (gl_LightSource[0].constantAttenuation +
				gl_LightSource[0].linearAttenuation * dist +
				gl_LightSource[0].quadraticAttenuation * dist * dist);
		color += att * (diffuse * NdotL + ambient);
	
//		halfV = normalize(halfVector);
		halfV = halfVector;
//		NdotHV = max(dot(n,halfV), 0.0);	// wrong, leaves tangent artifacts!
		NdotHV = dot(n,halfV);
		color += att * gl_FrontMaterial.specular * 
				gl_LightSource[0].specular * 
				pow(NdotHV, gl_FrontMaterial.shininess);
	}

	return color;
}

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

float lookup( vec2 offSet)
{
	// Values are multiplied by ShadowCoord.w because shadow2DProj does a W division for us.
	return shadow2DProj(shadowMap, 
				ShadowCoord + vec4(
						offSet.x * pixelOffset.x * ShadowCoord.w, 
						offSet.y * pixelOffset.y * ShadowCoord.w, 
						0.05, 0.0) ).w;
}

void main()
{	
	float shadow = 1.0;
#if 1
	// Avoid counter shadow
	//if (ShadowCoord.w > 0.0)
	 {
		// Simple lookup, no PCF
		//shadow = lookup(vec2(0.0,0.0));

		// 8x8 kernel PCF
		/*
		float x,y;
		for (y = -3.5 ; y <=3.5 ; y+=1.0)
			for (x = -3.5 ; x <=3.5 ; x+=1.0)
				shadow += lookup(vec2(x,y));
		shadow /= 64.0 ;
		*/

		// 8x8 PCF wide kernel (step is 10 instead of 1)
		/*
		float x,y;
		for (y = -30.5 ; y <=30.5 ; y+=10.0)
			for (x = -30.5 ; x <=30.5 ; x+=10.0)
				shadow += lookup(vec2(x,y));
		shadow /= 64.0 ;
		*/

		// 4x4 kernel PCF
		
		float x,y;
		for (y = -1.5 ; y <=1.5 ; y+=1.0)
			for (x = -1.5 ; x <=1.5 ; x+=1.0)
				shadow += lookup(vec2(x,y));
		shadow /= 16.0 ;
		
		// 4x4  PCF wide kernel (step is 10 instead of 1)
		/*
		float x,y;
		for (y = -10.5 ; y <=10.5 ; y+=10.0)
			for (x = -10.5 ; x <=10.5 ; x+=10.0)
				shadow += lookup(vec2(x,y));
		shadow /= 16.0 ;
		*/
		
		// 4x4  PCF dithered
		/*
		// use modulo to vary the sample pattern
		vec2 o = mod(floor(gl_FragCoord.xy), 2.0);
	
		shadow += lookup(vec2(-1.5, 1.5) + o);
		shadow += lookup(vec2( 0.5, 1.5) + o);
		shadow += lookup(vec2(-1.5, -0.5) + o);
		shadow += lookup(vec2( 0.5, -0.5) + o);
		shadow *= 0.25 ;
		*/
	}
#endif

	vec4 tex = texture2D(tex0, texCoord);
	vec4 light = pointlight();
	tex = mix(tex * light, vec4(0.0,0.0,0.0,1.0), 0.6-shadow);
	gl_FragColor = tex;
}
